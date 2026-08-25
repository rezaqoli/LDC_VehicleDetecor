// ============================================================
// Quectel EC200U LTE Module Driver - Implementation
// ============================================================
#include "EC200U_LTE.h"

// ========== Constructor/Destructor ==========
DeviceInfo dv_info;

EC200U_LTE::EC200U_LTE()
    : serial(nullptr), state(EC200UState::OFFLINE), initialized(false),
      debug_enabled(true), last_response_time_ms(0), boot_time_ms(0)
{
}

EC200U_LTE::~EC200U_LTE()
{
  end();
}

// ========== Initialization ==========
bool EC200U_LTE::begin()
{
  if (initialized)
    return true;

  Serial.println("[EC200U] Initializing...");

  // Setup hardware serial
  serial = &Serial1;
  serial->begin(EC200U_BAUD_RATE, SERIAL_8N1, EC200U_RX_PIN, EC200U_TX_PIN);

  // Setup pins
  //pinMode(EC200U_PWR_PIN, OUTPUT);
  pinMode(EC200U_RESET_PIN, OUTPUT);

  // Power on device
  if (!powerOn())
  {
    printDebug("[EC200U] Failed to power on");
    return false;
  }

  // Wait for boot
  uint32_t start = millis();
  bool boot_ok = false;
  while (millis() - start < AT_BOOT_TIMEOUT_MS)
  {
    ATResponse resp = sendCommand("AT", 1000);
    if (resp.success)
    {
      boot_time_ms = millis() - start;
      printDebug("[EC200U] Device booted in %lu ms", boot_time_ms);
      boot_ok = true;
      break;
    }
    delay(500);
  }

  if (!boot_ok)
  {
    printDebug("[EC200U] Boot timeout or failed handshake");
    state = EC200UState::ERROR_STATE;
    return false;
  }

  // Configure device
  atEcho(false);              // Disable echo
  atTest();                   // Verify connection
  getDeviceInfo(dv_info); // Get info

  state = EC200UState::IDLE;
  initialized = true;

  printDebug("[EC200U] Initialized successfully");
  return true;
}

void EC200U_LTE::end()
{
  if (serial)
  {
    powerOff();
    serial->end();
    serial = nullptr;
  }
  initialized = false;
  state = EC200UState::OFFLINE;
}

// ========== Power Management ==========
bool EC200U_LTE::powerOn()
{
  printDebug("[EC200U] Powering on...");

  // Pull PWR pin low for 500ms
  // digitalWrite(EC200U_PWR_PIN, HIGH);
  // delay(500);
  // digitalWrite(EC200U_PWR_PIN, LOW);
  // delay(100);

  digitalWrite(EC200U_RESET_PIN, HIGH);
  state = EC200UState::INITIALIZING;
  return true;
}

bool EC200U_LTE::powerOff()
{
  printDebug("[EC200U] Powering off...");

  // Send power off command
  sendCommand("AT+QPOWD=1", 2000);

  // Pull PWR pin to ensure power down
  // digitalWrite(EC200U_PWR_PIN, HIGH);
  // delay(1000);
  // digitalWrite(EC200U_PWR_PIN, LOW);
  digitalWrite(EC200U_RESET_PIN, LOW);

  state = EC200UState::OFFLINE;
  return true;
}

bool EC200U_LTE::hardReset()
{
  printDebug("[EC200U] Hard reset...");

  digitalWrite(EC200U_RESET_PIN, LOW);
  delay(500);
  digitalWrite(EC200U_RESET_PIN, HIGH);
  delay(100);

  state = EC200UState::INITIALIZING;
  return powerOn();
}

// ========== Basic AT Commands ==========
bool EC200U_LTE::atTest()
{
  ATResponse resp = sendCommand("AT");
  return resp.success;
}

bool EC200U_LTE::atEcho(bool enable)
{
  const char *cmd = enable ? "ATE1" : "ATE0";
  ATResponse resp = sendCommand(cmd);
  return resp.success;
}

bool EC200U_LTE::reset(bool hardReset_flag)
{
  if (hardReset_flag)
    return hardReset();

  ATResponse resp = sendCommand("ATZ");
  return resp.success;
}

bool EC200U_LTE::getDeviceInfo(DeviceInfo &info)
{
  // Get manufacturer
  ATResponse resp = sendCommand("AT+CGMI");
  if (resp.success && resp.line_count > 0)
  {
    strncpy(info.manufacturer, resp.lines[0], sizeof(info.manufacturer) - 1);
  }

  // Get model
  resp = sendCommand("AT+CGMM");
  if (resp.success && resp.line_count > 0)
  {
    strncpy(info.model, resp.lines[0], sizeof(info.model) - 1);
  }

  // Get firmware version
  resp = sendCommand("AT+CGMR");
  if (resp.success && resp.line_count > 0)
  {
    strncpy(info.fw_version, resp.lines[0], sizeof(info.fw_version) - 1);
  }

  // Get IMEI
  resp = sendCommand("AT+CGSN");
  if (resp.success && resp.line_count > 0)
  {
    strncpy(info.imei, resp.lines[0], sizeof(info.imei) - 1);
  }

  // Get IMSI
  resp = sendCommand("AT+CIMI");
  if (resp.success && resp.line_count > 0)
  {
    strncpy(info.imsi, resp.lines[0], sizeof(info.imsi) - 1);
  }

  printDebug("[EC200U] Device Info:");
  printDebug("  Mfg: %s", info.manufacturer);
  printDebug("  Model: %s", info.model);
  printDebug("  FW: %s", info.fw_version);
  printDebug("  IMEI: %s", info.imei);
  printDebug("  IMSI: %s", info.imsi);

  return true;
}

// ========== Network Registration ==========
bool EC200U_LTE::queryRegistrationStatus(NetworkInfo &info)
{
  // Check registration status: +CREG
  // Format: +CREG: <n>,<stat>[,<lac>,<ci>[,<AcT>]]
  ATResponse resp = sendCommand("AT+CREG?");
  if (!resp.success || resp.line_count == 0)
    return false;

  const char *line = resp.lines[0];
  int stat;
  if (sscanf(line, "+CREG: %*d,%d", &stat) != 1)
    return false;

  info.registered = (stat == 1 || stat == 5); // 1=reg home, 5=reg roaming

  // Get operator name: +COPS
  // Format: +COPS: <mode>,<format>,<oper>[,<AcT>]
  resp = sendCommand("AT+COPS?");
  if (resp.success && resp.line_count > 0)
  {
    line = resp.lines[0];
    extractStringParam(line, 2, info.operator_name, sizeof(info.operator_name));
  }

  // Get signal quality: +CSQ
  // Format: +CSQ: <rssi>,<ber>
  resp = sendCommand("AT+CSQ");
  if (resp.success && resp.line_count > 0)
  {
    line = resp.lines[0];
    sscanf(line, "+CSQ: %hhu,%hhu", &info.signal.rssi, &info.signal.ber);
  }

  // Get LTE signal quality if available: +QCSQ
  // Format: +QCSQ: "LTE",<rsrp>,<rsrq>,<sinr>
  resp = sendCommand("AT+QCSQ");
  if (resp.success && resp.line_count > 0)
  {
    line = resp.lines[0];
    int rsrp, rsrq, sinr;
    if (sscanf(line, "+QCSQ: \"LTE\",%d,%d,%d", &rsrp, &rsrq, &sinr) == 3)
    {
      info.signal.rsrp = rsrp;
      info.signal.rsrq = rsrq;
      info.signal.sinr = sinr;
      info.type = NetworkType::LTE_4G;
    }
  }

  return info.registered;
}

bool EC200U_LTE::waitForRegistration(uint32_t timeout_ms)
{
  uint32_t start = millis();
  NetworkInfo info;

  while (millis() - start < timeout_ms)
  {
    if (queryRegistrationStatus(info) && info.registered)
    {
      printDebug("[EC200U] Registered on %s", info.operator_name);
      state = (info.type == NetworkType::LTE_4G) ? EC200UState::CONNECTED_4G
                                                  : EC200UState::CONNECTED_2G;
      return true;
    }
    delay(1000);
  }

  printDebug("[EC200U] Registration timeout");
  return false;
}

bool EC200U_LTE::getSignalQuality(SignalQuality &quality)
{
  NetworkInfo info;
  if (!queryRegistrationStatus(info))
    return false;

  quality = info.signal;
  return true;
}

bool EC200U_LTE::setNetworkMode(NetworkType prefer)
{
  // AT+QCFG="nwscanmode",<mode>
  // mode: 0=auto, 1=2G, 2=4G
  uint8_t mode = (prefer == NetworkType::GSM_2G) ? 1 : 2;
  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+QCFG=\"nwscanmode\",%u", mode);
  return resp.success;
}

// ========== SIM Card ==========
bool EC200U_LTE::getSIMStatus()
{
  ATResponse resp = sendCommand("AT+CPIN?");
  if (!resp.success || resp.line_count == 0)
    return false;

  // +CPIN: READY (no PIN), SIM PIN, SIM PUK, etc.
  const char *line = resp.lines[0];
  bool ready = strstr(line, "READY") != nullptr;

  printDebug("[EC200U] SIM Status: %s", line);
  return ready;
}

bool EC200U_LTE::checkPIN()
{
  ATResponse resp = sendCommand("AT+CPIN?");
  return resp.success;
}

bool EC200U_LTE::enterPIN(const char *pin)
{
  if (!pin || strlen(pin) == 0)
    return false;

  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS, "AT+CPIN=\"%s\"", pin);
  return resp.success;
}

// ========== Data Connection ==========
bool EC200U_LTE::activatePDP(const char *apn)
{
  if (!apn)
    return false;

  // Set APN: AT+CGDCONT
  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
  if (!resp.success)
  {
    printDebug("[EC200U] Failed to set APN");
    return false;
  }

  // Activate PDP context: AT+CGACT
  resp = sendCommand("AT+CGACT=1,1");
  if (!resp.success)
  {
    printDebug("[EC200U] Failed to activate PDP");
    return false;
  }

  printDebug("[EC200U] PDP activated with APN: %s", apn);
  return true;
}

bool EC200U_LTE::deactivatePDP()
{
  ATResponse resp = sendCommand("AT+CGACT=0,1");
  return resp.success;
}

bool EC200U_LTE::getPDPStatus()
{
  ATResponse resp = sendCommand("AT+CGACT?");
  if (!resp.success || resp.line_count == 0)
    return false;

  // +CGACT: <id1>,<stat1>[,<id2>,<stat2>[...]]
  // stat: 0=inactive, 1=active
  const char *line = resp.lines[0];
  int stat;
  if (sscanf(line, "+CGACT: %*d,%d", &stat) == 1)
  {
    return stat == 1;
  }

  return false;
}

// ========== Socket Commands ==========
int32_t EC200U_LTE::openSocket(const char *host, uint16_t port, bool is_udp)
{
  if (!host)
    return -1;

  // AT+QIOPEN: Open network socket
  // AT+QIOPEN=<contextID>,<connectID>,"TCP/UDP",<remoteIP>,<remotePort>
  const char *protocol = is_udp ? "UDP" : "TCP";
  ATResponse resp = sendCommandFmt(5000,
                                   "AT+QIOPEN=1,0,\"%s\",\"%s\",%u", protocol, host, port);

  if (resp.success)
  {
    // Parse socket ID from response
    // +QIOPEN: <connect_id>,<err>
    if (resp.line_count > 0)
    {
      int connect_id, err;
      if (sscanf(resp.lines[0], "+QIOPEN: %d,%d", &connect_id, &err) == 2)
      {
        if (err == 0)
        {
          printDebug("[EC200U] Socket opened: ID=%d, %s:%u", connect_id, host, port);
          return connect_id;
        }
      }
    }
  }

  printDebug("[EC200U] Failed to open socket to %s:%u", host, port);
  return -1;
}

bool EC200U_LTE::closeSocket(int32_t socket_id)
{
  if (socket_id < 0)
    return false;

  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+QICLOSE=%ld", socket_id);
  return resp.success;
}

bool EC200U_LTE::sendData(int32_t socket_id, const uint8_t *data, uint16_t len)
{
  if (socket_id < 0 || !data || len == 0)
    return false;

  // Send: AT+QISEND=<connectID>,<sendlen>
  // Then wait for prompt and send data
  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+QISEND=%ld,%u", socket_id, len);

  if (!resp.success)
    return false;

  // Send data
  if (serial)
  {
    serial->write(data, len);
    serial->flush();

    // Wait for OK
    char buffer[256];
    if (waitForResponse(buffer, sizeof(buffer), 5000))
    {
      return strstr(buffer, "OK") != nullptr;
    }
  }

  return false;
}

uint16_t EC200U_LTE::receiveData(int32_t socket_id, uint8_t *buffer, uint16_t max_len)
{
  if (socket_id < 0 || !buffer || max_len == 0)
    return 0;

  // Unsolicited receive: +QIURC: "recv",<connectID>,<dataLen>
  // Then use: AT+QIRECV=<connectID>,<readLen>
  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+QIRECV=%ld,%u", socket_id, max_len);

  if (!resp.success || resp.line_count == 0)
    return 0;

  // Parse received data length and content
  // Format: +QIRECV: <len>\r\n<data>
  const char *line = resp.lines[0];
  uint16_t recv_len = 0;

  if (sscanf(line, "+QIRECV: %hu", &recv_len) == 1)
  {
    if (recv_len > max_len)
      recv_len = max_len;

    // Copy data from response
    if (resp.line_count > 1)
    {
      memcpy(buffer, resp.lines[1], recv_len);
    }

    return recv_len;
  }

  return 0;
}

bool EC200U_LTE::setReceiveCallback(int32_t socket_id, void (*callback)(const uint8_t *, uint16_t))
{
  // This would require setting up UART interrupt handler
  // Stub for now
  (void)socket_id;
  (void)callback;
  return true;
}

bool EC200U_LTE::setPowerMode(uint8_t mode)
{
  // AT+QSCLK: Enable/Disable sleep mode
  // mode: 0=disable, 1=enable
  ATResponse resp = sendCommandFmt(AT_CMD_TIMEOUT_MS,
                                   "AT+QSCLK=%u", mode);
  return resp.success;
}

bool EC200U_LTE::sleep()
{
  return setPowerMode(1);
}

bool EC200U_LTE::wakeup()
{
  return setPowerMode(0);
}

// ========== Low-level AT Interface ==========
ATResponse EC200U_LTE::sendCommand(const char *cmd, uint32_t timeout_ms)
{
  return sendCommandFmt(timeout_ms, "%s", cmd);
}

ATResponse EC200U_LTE::sendCommandFmt(uint32_t timeout_ms, const char *fmt, ...)
{
  ATResponse result = {};
  result.success = false;

  if (!serial || (state == EC200UState::OFFLINE && !initialized))
  {
    result.success = false;
    return result;
  }

  // Format command
  char cmd[AT_TX_BUFFER_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(cmd, sizeof(cmd), fmt, args);
  va_end(args);

  // Send command
  flushBuffer();
  printDebug("[EC200U TX] %s", cmd);

  serial->print(cmd);
  serial->print("\r\n");
  serial->flush();

  // Wait for response
  char response[AT_RX_BUFFER_SIZE];
  uint32_t start = millis();

  if (!waitForResponse(response, sizeof(response), timeout_ms))
  {
    result.success = false;
    printDebug("[EC200U] No response to: %s", cmd);
    return result;
  }

  result.response_time_ms = millis() - start;

  // Parse response
  if (!parseATResponse(response, result))
  {
    result.success = false;
  }

  if (debug_enabled && result.line_count > 0)
  {
    for (uint16_t i = 0; i < result.line_count && i < 5; i++)
    {
      printDebug("[EC200U RX] %s", result.lines[i]);
    }
  }

  return result;
}

void EC200U_LTE::flushBuffer()
{
  if (!serial)
    return;

  while (serial->available())
  {
    serial->read();
  }
}

// ========== Internal Helpers ==========
bool EC200U_LTE::waitForResponse(char *buffer, size_t max_len, uint32_t timeout_ms)
{
  if (!serial || !buffer || max_len == 0)
    return false;

  uint32_t start = millis();
  size_t pos = 0;
  bool found_ok = false;
  bool found_error = false;

  memset(buffer, 0, max_len);

  while (millis() - start < timeout_ms)
  {
    if (serial->available())
    {
      int c = serial->read();
      if (c < 0)
        continue;

      buffer[pos++] = (char)c;
      if (pos >= max_len - 1)
        break;

      // Check for termination conditions
      if (pos >= 2)
      {
        if (buffer[pos - 2] == 'O' && buffer[pos - 1] == 'K')
          found_ok = true;
      }
      if (pos >= 5)
      {
        if (buffer[pos - 5] == 'E' && buffer[pos - 4] == 'R' &&
            buffer[pos - 3] == 'R' && buffer[pos - 2] == 'O' && buffer[pos - 1] == 'R')
          found_error = true;
      }

      if (found_ok || found_error)
        break;
    }
    else
    {
      delay(10);
    }
  }

  buffer[pos] = '\0';
  last_response_time_ms = millis() - start;

  return pos > 0 && (found_ok || found_error);
}

bool EC200U_LTE::parseATResponse(const char *response, ATResponse &result)
{
  if (!response)
    return false;

  memset(result.lines, 0, sizeof(result.lines));
  result.line_count = 0;
  result.success = false;

  const char *p = response;
  const char *line_start = p;

  while (*p && result.line_count < AT_RESPONSE_LINES)
  {
    // Look for line terminators
    if (*p == '\r' || *p == '\n')
    {
      size_t line_len = p - line_start;

      // Skip empty lines
      if (line_len > 0)
      {
        // Copy line, trimming whitespace
        char *line_buf = result.lines[result.line_count];
        size_t copy_len = (line_len < sizeof(result.lines[0]) - 1) ? line_len : (sizeof(result.lines[0]) - 1);

        strncpy(line_buf, line_start, copy_len);
        line_buf[copy_len] = '\0';

        // Trim trailing whitespace
        for (int i = strlen(line_buf) - 1; i >= 0; i--)
        {
          if (line_buf[i] == '\r' || line_buf[i] == '\n' || line_buf[i] == ' ')
            line_buf[i] = '\0';
          else
            break;
        }

        // Check for OK/ERROR
        if (strcmp(line_buf, "OK") == 0)
        {
          result.success = true;
          result.line_count++;
          return true;
        }
        else if (strcmp(line_buf, "ERROR") == 0 || strstr(line_buf, "+CME ERROR"))
        {
          result.success = false;
          result.line_count++;
          return false;
        }
        else if (strlen(line_buf) > 0)
        {
          result.line_count++;
        }
      }

      // Skip all consecutive line terminators
      while (*p == '\r' || *p == '\n')
        p++;
      line_start = p;
    }
    else
    {
      p++;
    }
  }

  return result.success;
}

// ========== Parsing Helpers ==========
bool EC200U_LTE::extractStringParam(const char *line, uint8_t param_idx, char *out, size_t out_len)
{
  if (!line || !out || out_len == 0)
    return false;

  const char *p = line;
  uint8_t current_param = 0;
  bool in_quotes = false;

  while (*p && current_param <= param_idx)
  {
    if (*p == '"')
    {
      in_quotes = !in_quotes;
      if (current_param == param_idx && in_quotes)
      {
        // Start of parameter string
        p++;
        size_t len = 0;
        while (*p && *p != '"' && len < out_len - 1)
        {
          out[len++] = *p++;
        }
        out[len] = '\0';
        return true;
      }
    }
    else if (*p == ',' && !in_quotes)
    {
      current_param++;
    }

    p++;
  }

  return false;
}

bool EC200U_LTE::extractIntParam(const char *line, uint8_t param_idx, int32_t &out)
{
  if (!line)
    return false;

  const char *p = line;
  uint8_t current_param = 0;
  bool in_quotes = false;

  while (*p)
  {
    if (*p == '"')
    {
      in_quotes = !in_quotes;
    }
    else if (*p == ',' && !in_quotes)
    {
      current_param++;
    }
    else if (current_param == param_idx && !in_quotes && isdigit(*p))
    {
      return sscanf(p, "%ld", &out) == 1;
    }

    p++;
  }

  return false;
}

bool EC200U_LTE::extractHexParam(const char *line, uint8_t param_idx, uint32_t &out)
{
  if (!line)
    return false;

  const char *p = line;
  uint8_t current_param = 0;

  while (*p)
  {
    if (*p == ',')
    {
      current_param++;
    }
    else if (current_param == param_idx)
    {
      return sscanf(p, "0x%x", &out) == 1 || sscanf(p, "%x", &out) == 1;
    }

    p++;
  }

  return false;
}

// ========== State Management ==========
const char *EC200U_LTE::getStateString() const
{
  switch (state)
  {
  case EC200UState::OFFLINE:
    return "OFFLINE";
  case EC200UState::INITIALIZING:
    return "INITIALIZING";
  case EC200UState::IDLE:
    return "IDLE";
  case EC200UState::CONNECTED_2G:
    return "CONNECTED_2G";
  case EC200UState::CONNECTED_4G:
    return "CONNECTED_4G";
  case EC200UState::ERROR_STATE:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

// ========== Debug ==========
void EC200U_LTE::printDebug(const char *fmt, ...)
{
  if (!debug_enabled)
    return;

  va_list args;
  va_start(args, fmt);
  Serial.print("[EC200U] ");
  vprintf(fmt, args);
  Serial.println();
  va_end(args);
}
