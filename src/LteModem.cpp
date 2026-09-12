#include "LteModem.h"
#include "WsUtils.h"
#include <cstring>
#include <ctime>

char lte_apn[32] = "shatelmobile";

TinyGsm modem(SerialAT);
TinyGsmClient lteClient(modem);
SemaphoreHandle_t modemMutex = nullptr;

bool lteInitialized = false;
bool lteGprsConnected = false;

// When true the OTA task owns the modem exclusively.  Every other
// task must not touch the modem at all — takeModem() bails out
// instantly and the task loops sleep until the flag is cleared.
static volatile bool  s_otaModemExclusive = false;
static TaskHandle_t   s_otaOwnerTask      = nullptr;

void otaSetModemExclusive(bool exclusive, TaskHandle_t ownerTask)
{
  s_otaModemExclusive = exclusive;
  s_otaOwnerTask      = exclusive ? (ownerTask ? ownerTask : xTaskGetCurrentTaskHandle()) : nullptr;
  Serial.printf("[LTE] OTA exclusive %s\n", exclusive ? "ON — all modem tasks paused" : "OFF — modem tasks resumed");
}

bool otaIsModemExclusive()
{
  return s_otaModemExclusive;
}

bool modemMutexReady()
{
  return modemMutex != nullptr;
}

bool takeModem(uint32_t timeoutMs)
{
  if (s_otaModemExclusive && xTaskGetCurrentTaskHandle() != s_otaOwnerTask)
  {
    // OTA is flashing — only the OTA task itself may touch the modem.
    // All other tasks get an instant "busy" without blocking.
    return false;
  }
  if (modemMutex == nullptr)
  {
    Serial.println("[LTE] WARN: takeModem before modemMutex is initialised");
    return false;
  }
  if (xSemaphoreTake(modemMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
  {
    Serial.printf("[LTE] WARN: takeModem timeout after %u ms\n", (unsigned)timeoutMs);
    return false;
  }
  return true;
}

void giveModem()
{
  if (modemMutex)
    xSemaphoreGive(modemMutex);
}

// Create the modem mutex up front, before any task tries to take it.
// Safe to call multiple times; subsequent calls are no-ops.
void modemMutexInit()
{
  if (modemMutex != nullptr)
    return;
  modemMutex = xSemaphoreCreateMutex();
  if (!modemMutex)
  {
    Serial.println("[LTE] FATAL: failed to create modem mutex");
  }
  else
  {
    Serial.println("[LTE] modemMutex initialised");
  }
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
  if (!modem.testAT(10000L))
  {
    Serial.println("[LTE] FATAL: Modem did not answer AT");
    wsSend("[LTE] FATAL: Modem did not answer AT");
    return false;
  }
  modem.sendAT("E0");
  if (modem.waitResponse(3000L) != 1)
    Serial.println("[LTE] Warning: failed to disable echo");

  modem.sendAT("+CREG=0"); // Disable network registration URCs
  modem.waitResponse(2000L);
  modem.sendAT("+CGREG=0"); // Disable GPRS registration URCs
  modem.waitResponse(2000L);
  // modem.sendAT("+CMEE=2"); // Enable verbose error codes
  // modem.waitResponse(3000L);

  Serial.printf("[LTE] Modem: %s\n", modem.getModemName().c_str());

  SimStatus simStatus = modem.getSimStatus(15000L);

  // --- ADD THIS DEBUG LINE ---
  Serial.printf("[LTE] DEBUG: SimStatus Enum Value = %d\n", (int)simStatus);
  // ---------------------------

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
    // Optional: Print more info if available
    // Serial.printf("[LTE] Last Error: %s\n", modem.getLastError().c_str());
    return false;
  }
  return true;
}

// ============================================================
// Hardware reset of the LTE modem (toggle MODEM_RESET_PIN)
// Caller must NOT hold modemMutex.
// ============================================================
static void hardwareResetModem()
{
  Serial.println("[LTE] Hardware-resetting modem (MODEM_RESET_PIN)...");
  wsSend("[LTE] Hardware-resetting modem (MODEM_RESET_PIN)...");
  digitalWrite(MODEM_RESET_PIN, LOW);
  vTaskDelay(pdMS_TO_TICKS(300));
  digitalWrite(MODEM_RESET_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(3000));
  lteInitialized = false;
  lteGprsConnected = false;
}

// ============================================================
// Soft reset: drop PDP context + re-register on the network.
// Caller MUST hold modemMutex.
// ============================================================
static void softResetDataConnection()
{
  Serial.println("[LTE] Resetting data connection...");
  wsSend("[LTE] Resetting data connection...");
  // Bring the PDP context down and back up. Falls back to hardware reset on
  // repeated failure (handled in the status monitor via the counter).
  modem.gprsDisconnect();
  vTaskDelay(pdMS_TO_TICKS(500));
  lteGprsConnected = modem.gprsConnect(lte_apn, "", "");
  Serial.println(lteGprsConnected ? "[LTE] Data connection re-established"
                                   : "[LTE] Data connection still DOWN");
  wsSend(lteGprsConnected ? "[LTE] Data connection re-established"
                           : "[LTE] Data connection still DOWN");
}

void taskLTEInit(void *)
{
  Serial.println("\n[LTE] Starting TinyGSM initialization...");
  wsSend("\n[LTE] Starting TinyGSM initialization...");

  if (!modemMutexReady())
  {
    Serial.println("[LTE] FATAL: modemMutex not initialised — call modemMutexInit() in setup()");
    wsSend("[LTE] FATAL: modemMutex not initialised");
    vTaskDelete(nullptr);
    return;
  }

  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(3000));

  // HardwareSerial only accepts buffer resizing before begin().  Calling
  // setRxBufferSize() after begin() silently leaves the small default buffer
  // in place, which corrupts large +QIRD responses during OTA downloads.
  SerialAT.setRxBufferSize(8192);
  SerialAT.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  vTaskDelay(pdMS_TO_TICKS(300));

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

  Serial.printf("[LTE] Connecting APN: %s\n", lte_apn);
  lteGprsConnected = modem.gprsConnect(lte_apn, "", "");
  Serial.println(lteGprsConnected ? "[LTE] Data connection active" : "[LTE] FATAL: Data connection failed");
  wsSend(lteGprsConnected ? "[LTE] Data connection active" : "[LTE] FATAL: Data connection failed");

  giveModem();
  vTaskDelete(nullptr);
}

void taskLTEStatusMonitor(void *)
{
  TickType_t wake = xTaskGetTickCount();

  // Number of consecutive status ticks (one per minute) where data has been
  // disabled. When this hits the threshold, the modem is hardware-reset.
  static const uint8_t DATA_DOWN_THRESHOLD = 20; // 20 ticks * 60s = 20 min
  uint8_t dataDownStreak = 0;

  while (true)
  {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(60000));

    if (otaIsModemExclusive()) { dataDownStreak = 0; continue; } // don't fight OTA
    if (!lteInitialized)
    {
      dataDownStreak = 0;
      continue;
    }
    if (!takeModem(1000))
      continue;

    const bool networkConnected = modem.isNetworkConnected();
    const bool gprsConnected    = modem.isGprsConnected();
    lteGprsConnected            = gprsConnected;
    const int   signal          = modem.getSignalQuality();
    const String operatorName   = modem.getOperator();

    Serial.printf("[LTE Status] Network:%s Data:%s Signal:%d/31 Operator:%s (dataDownStreak=%u/%u)\n",
                  networkConnected ? "YES" : "NO",
                  gprsConnected    ? "YES" : "NO",
                  signal,
                  operatorName.c_str(),
                  (unsigned)dataDownStreak,
                  (unsigned)DATA_DOWN_THRESHOLD);

    if (!gprsConnected)
    {
      dataDownStreak++;

      if (dataDownStreak >= DATA_DOWN_THRESHOLD)
      {
        // Release the mutex before the hardware reset (which touches GPIO only).
        giveModem();
        Serial.printf("[LTE] Data has been DOWN for %u minutes — resetting modem.\n",
                      (unsigned)dataDownStreak);
        wsSend("[LTE] Data has been DOWN for 20 minutes — resetting modem.");
        hardwareResetModem();
        dataDownStreak = 0;
        continue;
      }

      // Soft-recover via PDP context bounce on the first failures.
      // softResetDataConnection() can block for many seconds — release the
      // modem mutex first so publish / WS paths are not starved.
      const bool doSoftReset = (dataDownStreak == 1 || dataDownStreak == 5 || dataDownStreak == 10);
      if (doSoftReset)
      {
        giveModem();
        bool ok = false;
        if (takeModem(15000))
        {
          ok = lteGprsConnected;  // value we just read is still authoritative
          // Note: we re-read after a fresh gprsConnect() inside the helper.
          giveModem();
        }
        // Run the long operation without holding the mutex
        softResetDataConnection();
        // Re-acquire briefly to refresh lteGprsConnected under protection
        if (takeModem(1000))
        {
          // softResetDataConnection already updated lteGprsConnected
          if (lteGprsConnected)
            dataDownStreak = 0;
          giveModem();
        }
        (void)ok;
        continue;
      }
    }
    else
    {
      // Healthy: clear the streak counter.
      dataDownStreak = 0;
    }

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
        modem.sendAT(line.substring(2));
        modem.waitResponse(10000L);
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
      Serial.println("  AT<command>  - Send raw AT command through TinyGSM");
      Serial.println("  LTE_STATUS   - Show network/data status");
      Serial.println("  LTE_INFO     - Show modem identity");
      Serial.println("  HELP_LTE     - Show this help");
      Serial.println();
    }
  }
}

// ============================================================
// GNSS (EC200U built-in)  —  lazy, on-demand, auto power-off
// ============================================================
static volatile GnssState s_gnssState      = GnssState::OFF;
static volatile uint32_t  s_gnssLastReqMs  = 0;
static GnssFix            s_gnssLastFix    = {};

void gnssInit()
{
  s_gnssState     = GnssState::OFF;
  s_gnssLastReqMs = 0;
  memset(&s_gnssLastFix, 0, sizeof(s_gnssLastFix));
}

GnssState gnssGetState()             { return s_gnssState; }
uint32_t  gnssGetLastRequestMs()     { return s_gnssLastReqMs; }
const GnssFix &gnssGetLastFix()      { return s_gnssLastFix; }

static const uint32_t GNSS_FIX_TIMEOUT_MS = 8000UL;

static void gnssFormatUtcTimestamp(char *buf, size_t len)
{
  time_t now = time(nullptr);
  if (now > 1609459200L)
  {
    struct tm ti;
    gmtime_r(&now, &ti);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &ti);
  }
  else
  {
    snprintf(buf, len, "BOOT+%lu", (unsigned long)millis());
  }
}

bool gnssGetFix(GnssFix &out)
{
  out = {};
  s_gnssLastReqMs = millis();

  if (!lteInitialized)
  {
    Serial.println("[GNSS] Modem not initialised");
    return false;
  }

  // Lazy power-on
  if (s_gnssState == GnssState::OFF)
  {
    Serial.println("[GNSS] Powering on (lazy enable)...");
    if (!takeModem(10000))
    {
      Serial.println("[GNSS] Modem busy for enable");
      return false;
    }
    bool ok = modem.enableGPS();
    giveModem();
    if (!ok)
    {
      Serial.println("[GNSS] enableGPS() returned false");
      return false;
    }
    s_gnssState = GnssState::STARTING;
  }

  // Single 8 s fix attempt.  modem.getGPS() blocks the UART.
  if (!takeModem(GNSS_FIX_TIMEOUT_MS + 1000))
  {
    Serial.println("[GNSS] Modem busy for fix");
    return false;
  }

  float    lat = 0, lon = 0, speed = 0, alt = 0, acc = 0;
  int      vsat = 0, usat = 0;
  int      year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

  bool ok = modem.getGPS(&lat, &lon, &speed, &alt,
                          &vsat, &usat, &acc,
                          &year, &month, &day,
                          &hour, &minute, &second);
  giveModem();

  if (!ok)
  {
    Serial.println("[GNSS] No fix yet (cold start or no satellites)");
    return false;
  }

  out.valid         = true;
  out.latitude      = lat;
  out.longitude     = lon;
  out.altitude_m    = alt;
  out.speed_kmh     = speed;
  out.satellites    = (usat > 0) ? (uint8_t)usat : (uint8_t)vsat;
  out.accuracy_m    = acc;
  out.sampled_at_ms = millis();

  // Prefer the modem's own time (UTC) if it produced it.
  if (year > 2000)
  {
    snprintf(out.timestamp, sizeof(out.timestamp),
             "%04d-%02d-%02dT%02d:%02d:%02dZ",
             year, month, day, hour, minute, second);
  }
  else
  {
    gnssFormatUtcTimestamp(out.timestamp, sizeof(out.timestamp));
  }

  s_gnssLastFix = out;
  s_gnssState   = GnssState::RUNNING;

  Serial.printf("[GNSS] Fix: %.5f, %.5f alt=%.0fm spd=%.1fkmh sats=%u acc=%.0fm %s\n",
                lat, lon, alt, speed, out.satellites, acc, out.timestamp);
  return true;
}

void gnssShutdown()
{
  if (s_gnssState == GnssState::OFF) return;
  Serial.println("[GNSS] Powering down");
  if (takeModem(5000))
  {
    modem.disableGPS();
    giveModem();
  }
  s_gnssState = GnssState::OFF;
}

void taskGnssIdleWatcher(void *)
{
  Serial.println("[GNSS] Idle watcher started");
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (otaIsModemExclusive()) continue; // don't race OTA's modem use
    if (s_gnssState == GnssState::OFF) continue;
    if (s_gnssLastReqMs == 0)        continue;
    if ((millis() - s_gnssLastReqMs) > GNSS_IDLE_MS)
    {
      Serial.println("[GNSS] Idle timeout — auto power-off");
      gnssShutdown();
    }
  }
}
