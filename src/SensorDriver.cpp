// ============================================================
// SensorDriver.cpp  —  LDC1614 sensor configuration & reading
// ============================================================
#include "SensorDriver.h"

void configureSensor(TwoWire &bus, RAK12029_LDC1614_Inductive &ldc, ChannelLC &lc)
{
  for (int ch = 0; ch < 4; ch++)
  {
    ldc.LDC1614_set_conversion_time(bus, ch, lc.conversion_time[ch]);
    ldc.LDC1614_set_driver_current(bus, ch, lc.driver_current[ch]);
  }
  ldc.LDC1614_mutiple_channel_config(bus, lc.L[0], lc.C[0]);
}

void readSensorChannels(TwoWire &bus, SemaphoreHandle_t mtx,
                        RAK12029_LDC1614_Inductive &ldc, RawFrame &frame, uint8_t sensor)
{
  if (sensor >= 2 || !mtx)
    return;

  if (xSemaphoreTake(mtx, pdMS_TO_TICKS(4)) == pdTRUE)
  {
    for (uint8_t ch = 0; ch < 4; ch++)
    {
      uint32_t raw = 0;
      if (ldc.LDC1614_get_channel_result(bus, ch, &raw) == 0 && raw)
        frame.filtered[sensor][ch] = raw;
    }
    xSemaphoreGive(mtx);
  }
}
