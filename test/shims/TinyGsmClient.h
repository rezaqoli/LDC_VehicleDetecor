#pragma once
#include "hal/Hal.h"
#include "ArduinoString.h"
#include "Arduino.h"

class TinyGsm {
 public:
  int  isNetworkConnected() { return 0; }
  int  isGprsConnected()    { return 0; }
  int  getSignalQuality()   { return 0; }
  String getOperator()      { return String(""); }
  String getModemName()     { return String("Quectel"); }
  String getModemInfo()     { return String(""); }
  String getIMEI()          { return String("000000000000000"); }
  String getIMSI()          { return String(""); }
  int    testAT(uint32_t)   { return 1; }
  void   sendAT(const char *) {}
  void   sendAT(const __FlashStringHelper *) {}
  void   sendAT(const char *, const String &) {}
  void   sendAT(const char *, const char *) {}
  void   sendAT(const __FlashStringHelper *, const char *) {}
  int8_t waitResponse(uint32_t = 1000L, const __FlashStringHelper * = nullptr) { return 1; }
  int8_t waitResponse(uint32_t, const char *) { return 1; }
  bool   enableGPS() { return true; }
  bool   disableGPS() { return true; }
  bool   getGPS(float *lat, float *lon, float *speed = nullptr, float *alt = nullptr,
                 int *vsat = nullptr, int *usat = nullptr, float *acc = nullptr,
                 int *year = nullptr, int *month = nullptr, int *day = nullptr,
                 int *hour = nullptr, int *minute = nullptr, int *second = nullptr) {
    (void)lat; (void)lon; (void)speed; (void)alt; (void)vsat; (void)usat; (void)acc;
    (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second;
    return false;
  }
  int  streamGetIntBefore(char)        { return 0; }
  bool streamGetStringBefore(char, char *, size_t) { return false; }
  struct { int dummy; } stream;
};
static TinyGsm modem;

class TinyGsmClient {
 public:
  TinyGsmClient(TinyGsm &) {}
  int  connect(const char *, uint16_t) { return 0; }
  int  connected() { return 0; }
  int  available() { return 0; }
  int  read()      { return -1; }
  void stop()     {}
  void print(const char *) {}
  void println(const char *) {}
};
static TinyGsmClient lteClient(modem);
