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

void trafficMonitorSetAdjacent(uint8_t s1, uint8_t ch1,
                               uint8_t s2, uint8_t ch2,
                               bool enabled);
