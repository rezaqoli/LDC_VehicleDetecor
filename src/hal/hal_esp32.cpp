// ============================================================
// hal_esp32.cpp  —  on-device pass-throughs
// ============================================================
#include "hal/Hal.h"
#include <Arduino.h>

namespace hal {

  uint32_t millis_now() { return ::millis(); }
  void     sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

  uint32_t adcReadMilliVolts(int pin) { return ::analogReadMilliVolts(pin); }
  void     setAdcMock(int, uint32_t)  { /* no-op on device */ }

  int64_t  unixTimeSec() { return (int64_t)::time(nullptr); }
  void     setUnixTimeSec(int64_t) { /* not used on device */ }

}  // namespace hal
