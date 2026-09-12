// ============================================================
// TimeManager.cpp
//   - Tries worldtimeapi.org via HTTP first.
//   - Falls back to AT+CCLK? (modem RTC).
//   - Holds modemMutex for the whole sync so we don't fight MQTT/WS.
// ============================================================
#include "TimeManager.h"
#include "Config.h"
#include "LteModem.h"
#include "MqttHandler.h"
#include <sys/time.h>
#include <cstring>
#include <cstdlib>

static volatile bool    g_timeSynced   = false;
static volatile time_t  g_lastSyncTime = 0;

bool isTimeSynced()   { return g_timeSynced; }
time_t getLastSyncTime() { return g_lastSyncTime; }

time_t getEpochTime()
{
  return g_timeSynced ? time(nullptr) : 0;
}

void formatIsoTime(char *buf, size_t len, time_t t)
{
  if (!g_timeSynced) { buf[0] = '\0'; return; }
  if (t == 0) t = time(nullptr);
  struct tm *ti = localtime(&t);
  strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", ti);
}

void getEventTimestamp(char *buf, size_t len)
{
  if (g_timeSynced)
  {
    formatIsoTime(buf, len, 0);
  }
  else
  {
    snprintf(buf, len, "BOOT+%lu", (unsigned long)millis());
  }
}

// -------------------------------------------------------
// HTTP time sync via worldtimeapi.org
// -------------------------------------------------------
static bool syncTimeFromHttp()
{
  if (!lteGprsConnected) return false;

  if (!takeModem(15000))
  {
    Serial.println("[TIME] Modem busy for HTTP sync");
    return false;
  }

  TinyGsmClient httpClient(lteClient);
  Serial.println("[TIME] Connecting to worldtimeapi.org:80 ...");
  if (!httpClient.connect("worldtimeapi.org", 80))
  {
    Serial.println("[TIME] TCP connect failed");
    giveModem();
    return false;
  }

  httpClient.print("GET /api/ip HTTP/1.1\r\n");
  httpClient.print("Host: worldtimeapi.org\r\n");
  httpClient.print("User-Agent: ESP32-Detector/1.0\r\n");
  httpClient.print("Connection: close\r\n\r\n");

  uint32_t t0 = millis();
  String header;
  String body;
  bool    inBody = false;
  while ((millis() - t0) < 10000UL && httpClient.connected())
  {
    while (httpClient.available())
    {
      char c = httpClient.read();
      if (!inBody)
      {
        header += c;
        if (header.endsWith("\r\n\r\n")) { inBody = true; header = ""; }
      }
      else
      {
        body += c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  httpClient.stop();
  giveModem();

  if (body.length() == 0)
  {
    Serial.println("[TIME] Empty HTTP body");
    return false;
  }

  const char *u = strstr(body.c_str(), "\"unixtime\":");
  if (!u)
  {
    Serial.println("[TIME] unixtime not found in response");
    return false;
  }
  long unixtime = atol(u + 11);

  long rawOffset = 0;
  const char *o = strstr(body.c_str(), "\"raw_offset\":");
  if (o) rawOffset = atol(o + 13);

  // worldtimeapi's unixtime is already UTC.  The configured TZ is applied by
  // localtime()/strftime(), so adding raw_offset here would shift local time twice.
  time_t final = (time_t)unixtime;
  if (final < 1609459200L)
  {
    Serial.printf("[TIME] Rejecting time %ld (before 2021)\n", (long)final);
    return false;
  }

  struct timeval tv = { .tv_sec = final, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  setenv("TZ", TIME_TZ, 1);
  tzset();

  g_lastSyncTime = final;
  g_timeSynced   = true;

  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", localtime(&final));
  Serial.printf("[TIME] Synced via HTTP: %s (unix=%ld, raw_offset=%ld)\n",
                buf, (long)final, rawOffset);
  return true;
}

// -------------------------------------------------------
// Modem time sync via AT+CCLK?
// -------------------------------------------------------
static bool syncTimeFromModem()
{
  if (!takeModem(5000)) return false;

  modem.sendAT(GF("+CCLK?"));
  int8_t rsp = modem.waitResponse(3000L, GF("+CCLK: "));
  if (rsp != 1)
  {
    Serial.println("[TIME] AT+CCLK? failed");
    giveModem();
    return false;
  }

  String clk = modem.stream.readStringUntil('\n');
  giveModem();
  clk.trim();
  clk.replace("\"", "");

  int yy, mo, dd, hh, mn, ss, tz = 0;
  int n = sscanf(clk.c_str(), "%d/%d/%d,%d:%d:%d%d",
                 &yy, &mo, &dd, &hh, &mn, &ss, &tz);
  if (n < 6)
  {
    Serial.printf("[TIME] Failed to parse CCLK: %s\n", clk.c_str());
    return false;
  }

  int year = (yy <= 50) ? (yy + 2000) : (yy + 1900);
  struct tm ti = {0};
  ti.tm_year = year - 1900;
  ti.tm_mon  = mo - 1;
  ti.tm_mday = dd;
  ti.tm_hour = hh;
  ti.tm_min  = mn;
  ti.tm_sec  = ss;

  time_t final = mktime(&ti);
  if (final < 1609459200L)
  {
    Serial.println("[TIME] Modem time looks wrong, rejecting");
    return false;
  }

  struct timeval tv = { .tv_sec = final, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  setenv("TZ", TIME_TZ, 1);
  tzset();

  g_lastSyncTime = final;
  g_timeSynced   = true;

  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&final));
  Serial.printf("[TIME] Synced from modem: %s\n", buf);
  return true;
}

// -------------------------------------------------------
// FreeRTOS task
// -------------------------------------------------------
void taskTimeSync(void *)
{
  Serial.println("[TIME] Sync task started");
  vTaskDelay(pdMS_TO_TICKS(TIME_SYNC_INITIAL_MS));

  TickType_t wake = xTaskGetTickCount();
  for (;;)
  {
    //if (!syncTimeFromHttp())
    {
      //vTaskDelay(pdMS_TO_TICKS(2000));
      syncTimeFromModem();
    }

    if (g_timeSynced)
    {
      char buf[40];
      time_t now = time(nullptr);
      strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
      Serial.printf("[TIME] Now=%s, next sync in %lu min\n",
                    buf, (unsigned long)(TIME_SYNC_PERIOD_MS / 60000UL));
    }
    else
    {
      Serial.println("[TIME] Sync failed, retry next cycle");
    }

    vTaskDelayUntil(&wake, pdMS_TO_TICKS(TIME_SYNC_PERIOD_MS));
  }
}
