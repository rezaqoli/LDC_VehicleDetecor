// Arduino.h — minimal native-host stub. Only used in the [env:native] test build.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include <time.h>
#include "hal/Hal.h"
#include "ArduinoString.h"

// MSVC and old MinGW do not provide localtime_r / gmtime_r.
#if defined(_WIN32) && !defined(localtime_r)
static inline struct tm *localtime_r(const time_t *t, struct tm *out) {
  if (!out) return nullptr;
  struct tm *p = localtime(t);
  if (p) { *out = *p; return out; }
  return nullptr;
}
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) {
  if (!out) return nullptr;
  struct tm *p = gmtime(t);
  if (p) { *out = *p; return out; }
  return nullptr;
}
#endif

typedef bool  boolean;
typedef uint8_t  byte;
typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int8_t   s8_t;
typedef int16_t  s16_t;
typedef int32_t  s32_t;
typedef uint32_t TickType_t;
typedef int    portBASE_TYPE;
typedef int    BaseType_t;
typedef int    UBaseType_t;
typedef std::string __FlashStringHelper;

namespace std {
  constexpr size_t npos = static_cast<size_t>(-1);
}

#define PROGMEM
#define F(x) (x)
#define PSTR(x) (x)
#define PI 3.14159265358979323846f
#define RAD_TO_DEG 57.2957795130823208768f
#define DEG_TO_RAD 0.017453292519943295769f
#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define LSBFIRST 0

#define pgm_read_byte(addr)  (*(const uint8_t *)(addr))
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#define strcpy_P(dest, src)   strcpy((dest), (src))

#define yield()               do {} while (0)
#define delay(ms)              do {} while (0)
#define delayMicroseconds(us)  do {} while (0)
#define millis()               hal::millis_now()
#define micros()               (hal::millis_now() * 1000UL)
#define digitalWrite(p, v)     do {} while (0)
#define digitalRead(p)         0
#define pinMode(p, m)          do {} while (0)
#define analogRead(p)          ((int)hal::adcReadMilliVolts(p))
#define analogReadMilliVolts(p) (hal::adcReadMilliVolts(p))
#define analogReadResolution(b) do {} while (0)
#define analogSetAttenuation(a)  do {} while (0)
#define analogSetPinAttenuation(p, a) do {} while (0)

// FreeRTOS shim — production code uses these for timing. Stub.
#include "freertos/FreeRTOS.h"

// IPAddress (also re-exported by WiFi.h shim). Put it here so headers that
// only include Arduino.h still see the type.
class IPAddress {
 public:
  uint8_t _bytes[4] = {0, 0, 0, 0};
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    _bytes[0] = a; _bytes[1] = b; _bytes[2] = c; _bytes[3] = d;
  }
  bool fromString(const char *s) {
    if (!s) return false;
    int a, b, c, d;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
      _bytes[0] = a; _bytes[1] = b; _bytes[2] = c; _bytes[3] = d;
      return true;
    }
    return false;
  }
  String toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             _bytes[0], _bytes[1], _bytes[2], _bytes[3]);
    return String(buf);
  }
  operator uint32_t() const {
    return ((uint32_t)_bytes[0] << 24) | ((uint32_t)_bytes[1] << 16) |
           ((uint32_t)_bytes[2] << 8)  |  (uint32_t)_bytes[3];
  }
  bool operator==(const IPAddress &o) const {
    return memcmp(_bytes, o._bytes, 4) == 0;
  }
  bool operator!=(const IPAddress &o) const { return !(*this == o); }
};

class Serial_t {
 public:
  void   begin(unsigned long, int = 0, int = 0, int = 0) {}
  void   print(const char *) {}
  void   print(int, int = 10) {}
  void   print(float, int = 2) {}
  void   print(char) {}
  void   print(const __FlashStringHelper *) {}
  void   println(const char *) {}
  void   println(int, int = 10) {}
  void   println(float, int = 2) {}
  void   println(char) {}
  void   println(const __FlashStringHelper *) {}
  void   printf(const char *, ...) {}
  int    available() { return 0; }
  int    read() { return -1; }
  void   flush() {}
};
static Serial_t Serial;

// Wire / TwoWire stubs (not used in host tests but referenced by code).
// TwoWire is provided by Wire.h shim (avoid duplicate symbol).
class SPISettings_t {};
class SPIClass_t {
 public:
  void   begin(int, int, int, int) {}
  void   end() {}
  void   beginTransaction(SPISettings_t) {}
  void   endTransaction() {}
  uint8_t transfer(uint8_t v) { return v; }
};
static SPIClass_t SPI;
