#include "LteModem.h"
#include "WsUtils.h"

TinyGsm modem(SerialAT);
TinyGsmClient lteClient(modem);
SemaphoreHandle_t modemMutex = nullptr;

bool lteInitialized = false;
bool lteGprsConnected = false;

bool takeModem(uint32_t timeoutMs)
{
  return modemMutex && xSemaphoreTake(modemMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void giveModem()
{
  if (modemMutex)
    xSemaphoreGive(modemMutex);
}

// -------------------------------------------------------
// Helper: drain UART completely
// -------------------------------------------------------
static void drainUart(uint32_t timeoutMs)
{
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialAT.available()) {
      SerialAT.read();
      start = millis(); // reset timeout if we got data
    }
    delay(2);
  }
}

// -------------------------------------------------------
// Helper: send raw AT command and wait for response
// Returns true if expected response found
// -------------------------------------------------------
static bool sendAtAndWait(const char* cmd, const char* expected, uint32_t timeoutMs)
{
  SerialAT.print(cmd);
  SerialAT.print("\r\n");
  SerialAT.flush();

  uint32_t start = millis();
  String buffer = "";

  while (millis() - start < timeoutMs) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (c == '\r' || c == '\n') {
        buffer.trim();
        if (buffer.length() > 0) {
          // Debug: log what we got
          // Serial.printf("[AT-RX] '%s'\n", buffer.c_str());

          if (buffer.indexOf(expected) >= 0) {
            return true;
          }
          // If we see ERROR, abort early
          if (buffer.indexOf("ERROR") >= 0) {
            return false;
          }
          buffer = "";
        }
      } else {
        buffer += c;
      }
    }
    delay(1);
  }
  return false;
}

// -------------------------------------------------------
// Helper: disable echo with verification
// -------------------------------------------------------
static bool disableEcho()
{
  // Try up to 3 times
  for (int attempt = 0; attempt < 3; attempt++) {
    drainUart(100);

    // Send ATE0
    SerialAT.print("ATE0\r\n");
    SerialAT.flush();
    delay(200);

    // Drain response (should be "ATE0" echo + "OK" or just "OK")
    drainUart(300);

    // Now verify: send AT and check we do NOT see "AT" echoed back
    SerialAT.print("AT\r\n");
    SerialAT.flush();

    uint32_t start = millis();
    String buffer = "";
    bool sawAtEcho = false;
    bool sawOk = false;

    while (millis() - start < 2000) {
      while (SerialAT.available()) {
        char c = SerialAT.read();
        if (c == '\r' || c == '\n') {
          buffer.trim();
          if (buffer.length() > 0) {
            if (buffer == "AT") {
              sawAtEcho = true; // Echo is still ON!
            }
            if (buffer == "OK") {
              sawOk = true;
            }
            buffer = "";
          }
        } else {
          buffer += c;
        }
      }
      delay(1);
    }

    if (sawOk && !sawAtEcho) {
      Serial.println("[LTE] Echo disabled successfully");
      return true;
    }

    Serial.printf("[LTE] Echo disable attempt %d failed (sawOk=%d sawAtEcho=%d)\n",
                  attempt + 1, sawOk, sawAtEcho);
    delay(500);
  }

  Serial.println("[LTE] WARNING: Could not verify echo disabled");
  return false; // Continue anyway, might still work
}

static void printLteStatus()
{
  if (!takeModem(5000))
  {
    Serial.println("[LTE] Modem busy");
    return;
  }

  Serial.println("\n========== LTE Status ==========");
  Serial.printf("Initialized     : %s\n", lteInitialized ? "YES" : "NO");
  Serial.printf("Network         : %s\n", modem.isNetworkConnected() ? "CONNECTED" : "DISCONNECTED");
  Serial.printf("Data Connection : %s\n", modem.isGprsConnected() ? "ACTIVE" : "INACTIVE");
  Serial.printf("Operator        : %s\n", modem.getOperator().c_str());
  Serial.printf("Signal          : %d/31\n", modem.getSignalQuality());
  Serial.println("================================\n");

  giveModem();
}

static void printLteInfo()
{
  if (!takeModem(5000))
  {
    Serial.println("[LTE] Modem busy");
    return;
  }

  Serial.println("\n========== LTE Device Info ==========");
  Serial.printf("Modem : %s\n", modem.getModemName().c_str());
  Serial.printf("Info  : %s\n", modem.getModemInfo().c_str());
  Serial.printf("IMEI  : %s\n", modem.getIMEI().c_str());
  Serial.printf("IMSI  : %s\n", modem.getIMSI().c_str());
  Serial.println("=====================================\n");

  giveModem();
}

static bool initEc200u()
{
  // -------------------------------------------------
  // Phase 1: Sync - establish clean communication
  // -------------------------------------------------
  Serial.println("[LTE] Phase 1: Syncing with modem...");

  drainUart(500); // Clear any garbage

  // Send a few bare AT commands to wake up and sync
  for (int i = 0; i < 3; i++) {
    if (sendAtAndWait("AT", "OK", 2000)) {
      Serial.printf("[LTE] Sync OK on attempt %d\n", i + 1);
      break;
    }
    delay(300);
  }

  // -------------------------------------------------
  // Phase 2: Disable echo (CRITICAL)
  // -------------------------------------------------
  Serial.println("[LTE] Phase 2: Disabling echo...");
  disableEcho();

  // -------------------------------------------------
  // Phase 3: Disable URCs that spam the UART
  // -------------------------------------------------
  Serial.println("[LTE] Phase 3: Disabling URCs...");

  sendAtAndWait("AT+CREG=0", "OK", 2000);  // Disable network reg URCs
  delay(100);
  sendAtAndWait("AT+CGREG=0", "OK", 2000); // Disable GPRS reg URCs
  delay(100);
  sendAtAndWait("AT+CEREG=0", "OK", 2000); // Disable EPS reg URCs (LTE)
  delay(100);

  // Optional: verbose errors
  // sendAtAndWait("AT+CMEE=2", "OK", 2000);

  // -------------------------------------------------
  // Phase 4: Verify modem identity
  // -------------------------------------------------
  Serial.println("[LTE] Phase 4: Reading modem info...");

  // Use TinyGSM methods now that echo is off
  String modemName = modem.getModemName();
  Serial.printf("[LTE] Modem: %s\n", modemName.c_str());

  // -------------------------------------------------
  // Phase 5: Check SIM
  // -------------------------------------------------
  Serial.println("[LTE] Phase 5: Checking SIM...");

  SimStatus simStatus = modem.getSimStatus(15000L);
  Serial.printf("[LTE] SimStatus = %d\n", (int)simStatus);

  if (simStatus == SIM_LOCKED)
  {
    Serial.println("[LTE] FATAL: SIM is PIN locked");
    wsSend("[LTE] FATAL: SIM is PIN locked");
    return false;
  }
  if (simStatus != SIM_READY)
  {
    Serial.println("[LTE] FATAL: SIM is not ready");
    wsSend("[LTE] FATAL: SIM is not ready");
    return false;
  }

  Serial.println("[LTE] SIM ready");
  return true;
}

void taskLTEInit(void *)
{
  Serial.println("\n[LTE] Starting TinyGSM initialization...");
  wsSend("\n[LTE] Starting TinyGSM initialization...");

  if (!modemMutex) {
    modemMutex = xSemaphoreCreateMutex();
    if (!modemMutex)
    {
      Serial.println("[LTE] Failed to create modem mutex");
      wsSend("[LTE] Failed to create modem mutex");
      vTaskDelete(nullptr);
      return;
    }
  }

  pinMode(MODEM_RESET_PIN, OUTPUT);
  // EC200U reset: active-LOW pulse, then HIGH, wait for boot
  digitalWrite(MODEM_RESET_PIN, LOW);
  delay(300);
  digitalWrite(MODEM_RESET_PIN, HIGH);

  // Wait for modem boot - EC200U needs ~3-5s after reset
  Serial.println("[LTE] Waiting for modem boot (5s)...");
  delay(5000);

  // Clean UART start
  SerialAT.end();
  delay(200);
  SerialAT.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // Drain boot messages (RDY, APP RDY, +CPIN: READY, etc.)
  //Serial.println("[LTE] Draining boot URCs...");
  //drainUart(2000); // 2 seconds of draining

  if (!takeModem(1000))
  {
    Serial.println("[LTE] Failed to lock modem");
    wsSend("[LTE] Failed to lock modem");
    vTaskDelete(nullptr);
    return;
  }

  Serial.println("[LTE] Initializing modem...");
  wsSend("[LTE] Initializing modem...");

  if (!initEc200u())
  {
    giveModem();
    vTaskDelete(nullptr);
    return;
  }

  lteInitialized = true;
  Serial.printf("[LTE] IMEI: %s\n", modem.getIMEI().c_str());

  Serial.println("[LTE] Waiting for network registration...");
  wsSend("[LTE] Waiting for network registration...");
  if (!modem.waitForNetwork(60000L, true))
  {
    Serial.println("[LTE] FATAL: Network registration failed");
    giveModem();
    vTaskDelete(nullptr);
    return;
  }

  char msg[128];
  snprintf(msg, sizeof(msg), "[LTE] Operator: %s\n", modem.getOperator().c_str());
  Serial.printf(msg);
  wsSend(msg);
  snprintf(msg, sizeof(msg), "[LTE] Signal: %d/31\n", modem.getSignalQuality());
  Serial.printf(msg);
  wsSend(msg);

  Serial.printf("[LTE] Connecting APN: %s\n", MODEM_APN);
  lteGprsConnected = modem.gprsConnect(MODEM_APN, "", "");
  Serial.println(lteGprsConnected ? "[LTE] Data connection active" : "[LTE] FATAL: Data connection failed");
  wsSend(lteGprsConnected ? "[LTE] Data connection active" : "[LTE] FATAL: Data connection failed");

  giveModem();
  vTaskDelete(nullptr);
}

void taskLTEStatusMonitor(void *)
{
  TickType_t wake = xTaskGetTickCount();
  while (true)
  {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(60000));

    if (!lteInitialized || !takeModem(1000))
      continue;

    const bool networkConnected = modem.isNetworkConnected();
    const bool gprsConnected = modem.isGprsConnected();
    lteGprsConnected = gprsConnected;

    Serial.printf("[LTE Status] Network:%s Data:%s Signal:%d/31 Operator:%s\n",
                  networkConnected ? "YES" : "NO",
                  gprsConnected ? "YES" : "NO",
                  modem.getSignalQuality(),
                  modem.getOperator().c_str());

    giveModem();
  }
}

void taskLTECommandConsole(void *)
{
  while (true)
  {
    if (!Serial.available())
    {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    if (line.startsWith("AT"))
    {
      if (takeModem(5000))
      {
        // Use raw send for console to see full response
        SerialAT.print(line.substring(2));
        SerialAT.print("\r\n");
        SerialAT.flush();

        // Echo what we sent
        Serial.printf("[AT-TX] %s\n", line.substring(2).c_str());

        // Read response with timeout
        uint32_t start = millis();
        String buffer = "";
        while (millis() - start < 10000L) {
          while (SerialAT.available()) {
            char c = SerialAT.read();
            if (c == '\r' || c == '\n') {
              buffer.trim();
              if (buffer.length() > 0) {
                Serial.printf("[AT-RX] %s\n", buffer.c_str());
                buffer = "";
              }
            } else {
              buffer += c;
            }
          }
          delay(1);
        }
        giveModem();
      }
      else
      {
        Serial.println("[LTE] Modem busy");
      }
    }
    else if (line == "LTE_STATUS")
    {
      printLteStatus();
    }
    else if (line == "LTE_INFO")
    {
      printLteInfo();
    }
    else if (line == "HELP_LTE")
    {
      Serial.println("\nLTE Commands:");
      Serial.println("  AT<command>  - Send raw AT command");
      Serial.println("  LTE_STATUS   - Show network/data status");
      Serial.println("  LTE_INFO     - Show modem identity");
      Serial.println("  HELP_LTE     - Show this help");
      Serial.println();
    }
  }
}