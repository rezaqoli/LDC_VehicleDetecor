// ============================================================
// Tasks.h  —  FreeRTOS task declarations
// ============================================================
#pragma once

void taskSensorReading(void *);
void taskDetector(void *);
void taskSpeedMatch(void *);
void taskWebServer(void *);
void taskWsLoop(void *);
void taskStatsReporter(void *);
void taskPowerMonitor(void *);
void taskTimeSync(void *);
void taskGnssIdleWatcher(void *);