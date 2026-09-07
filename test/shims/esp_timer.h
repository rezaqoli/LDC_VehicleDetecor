#pragma once
#include <cstdint>
inline uint64_t esp_timer_get_time() { return (uint64_t)hal::millis_now() * 1000ULL; }
