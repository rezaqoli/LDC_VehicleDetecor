// ============================================================
// Hal.h  —  Thin hardware abstraction for host tests
// ============================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace hal {

  uint32_t millis_now();
  void     sleep_ms(uint32_t ms);

  // ADC read.  On ESP32 this is a pass-through to analogReadMilliVolts;
  // on the host the test can `hal::setAdcMock(pin, value_mv)`.
  uint32_t adcReadMilliVolts(int pin);
  void     setAdcMock(int pin, uint32_t value_mv);

  // Time-of-day stub.  0 means "system time not set yet".
  int64_t  unixTimeSec();
  void     setUnixTimeSec(int64_t t);

}  // namespace hal
