// Minimal WiFi.h stub for native tests
#pragma once
#include <cstdint>
#include <cstring>
#include <ArduinoString.h>

class WiFiClass {
 public:
  int   status()    { return 3; /* WL_CONNECTED */ }
  int   RSSI()      { return -50; }
  String SSID()     { return String("TestNet"); }
  String localIP()  { return String("192.168.1.1"); }
  void   begin(const char *, const char *) {}
  void   config(IPAddress, IPAddress, IPAddress) {}
  void   mode(int) {}
};

static WiFiClass WiFi;
