#pragma once

#include "Config.h"

#include <TinyGsmClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern TinyGsm modem;
extern TinyGsmClient lteClient;
extern SemaphoreHandle_t modemMutex;
extern bool lteInitialized;
extern bool lteGprsConnected;

bool takeModem(uint32_t timeoutMs);
void giveModem();
bool modemMutexReady();
void modemMutexInit();

// Called by OTA to make takeModem() immediately fail for all other callers
// except the OTA task itself (identified by its TaskHandle). While exclusive
// is true every non-OTA takeModem() returns false without waiting and the
// modem-using task loops sleep until the flag is cleared.
void otaSetModemExclusive(bool exclusive, TaskHandle_t ownerTask = nullptr);
bool otaIsModemExclusive();

// ============================================================
// GNSS (EC200U built-in).  OFF by default — powers on only
// when gnssGetFix() / GET_GPS is called, and auto-powers off
// after GNSS_IDLE_MS of inactivity.
// ============================================================
#define GNSS_IDLE_MS 120000UL

struct GnssFix
{
  bool     valid;
  float    latitude;
  float    longitude;
  float    altitude_m;
  float    speed_kmh;
  uint8_t  satellites;
  float    accuracy_m;
  char     timestamp[32];
  uint32_t sampled_at_ms;
};

enum class GnssState : uint8_t
{
  OFF      = 0,
  STARTING = 1,
  RUNNING  = 2,
};

void          gnssInit();
const GnssFix &gnssGetLastFix();
GnssState      gnssGetState();
uint32_t       gnssGetLastRequestMs();
bool           gnssGetFix(GnssFix &out);
void           gnssShutdown();

void taskLTEInit(void *);
void taskLTEStatusMonitor(void *);
void taskLTECommandConsole(void *);
void taskGnssIdleWatcher(void *);
