// ============================================================
// TimeManager.h  —  System clock synchronisation
// ============================================================
#pragma once
#include <Arduino.h>
#include <time.h>

bool     isTimeSynced();
time_t   getEpochTime();
void     formatIsoTime(char *buf, size_t len, time_t t);
void     getEventTimestamp(char *buf, size_t len);
time_t   getLastSyncTime();
void     taskTimeSync(void *);
