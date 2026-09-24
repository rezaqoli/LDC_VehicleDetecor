#pragma once

#include <Arduino.h>
#include "Config.h"
#include "VehicleDetector.h"

void trafficMonitorOnRawEvent(const EventResult &ev);
void trafficMonitorOnSingleEvent(const EventResult &ev);
void trafficMonitorOnDualMatch(uint8_t pairIdx,
                               const EventResult *e1,
                               const EventResult *e2,
                               float speed_kmh);

// Register a completed speed pair and, when a matching adjacent-lane pair is
// available, build one additional between-lines aggregate report.
bool trafficMonitorBuildBetweenLinesEvent(uint8_t pairIdx,
                                          const EventResult *e1,
                                          const EventResult *e2,
                                          float speed_kmh,
                                          char *out,
                                          size_t outSize);

void trafficMonitorSetAdjacent(uint8_t s1, uint8_t ch1,
                               uint8_t s2, uint8_t ch2,
                               bool enabled);

void trafficMonitorSyncFromGeometry();
