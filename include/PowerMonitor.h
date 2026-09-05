// ============================================================
// PowerMonitor.h  —  Battery / Solar voltage sampling
// ============================================================
#pragma once
#include <Arduino.h>

struct PowerReadings
{
  float    battery_v;
  float    solar_v;
  bool     battery_low;
  uint32_t sampled_at_ms;
  bool     valid;
};

void                powerMonitorInit();
const PowerReadings &powerMonitorGet();
void                taskPowerMonitor(void *);
