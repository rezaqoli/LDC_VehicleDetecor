// ============================================================
// hal_native.cpp  —  host build stubs
// ============================================================
#include "hal/Hal.h"
#include <cstring>
#include <chrono>
#include <map>

namespace hal {

  static std::map<int, uint32_t> s_adc;
  static int64_t                 s_unix = 0;
  static uint64_t                s_boot_ms = 0;

  uint32_t millis_now()
  {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return (uint32_t)duration_cast<milliseconds>(now).count();
  }

  void sleep_ms(uint32_t ms) { (void)ms; /* no-op on host */ }

  uint32_t adcReadMilliVolts(int pin)
  {
    auto it = s_adc.find(pin);
    return (it == s_adc.end()) ? 0 : it->second;
  }

  void setAdcMock(int pin, uint32_t value_mv) { s_adc[pin] = value_mv; }

  int64_t unixTimeSec() { return s_unix; }
  void    setUnixTimeSec(int64_t t) { s_unix = t; }

}  // namespace hal
