#pragma once

#include <Arduino.h>

// Start a modem-based firmware pull. The URL must be http://host[:port]/path.
// md5 may be empty; production deployments should always provide it.
bool otaStartFromModem(const char *url, const char *md5);
bool otaIsRunning();
