#pragma once

#include <Arduino.h>

// Start a modem-based firmware pull. The URL must be http://host[:port]/path or https://host[:port]/path.
// md5 may be empty; production deployments should always provide it (32 lowercase hex chars).
bool otaStartFromModem(const char *url, const char *md5);
bool otaIsRunning();

// Constants for external use (e.g., web app validation)
static constexpr uint32_t OTA_MAX_IMAGE_SIZE = 3UL * 1024UL * 1024UL;  // 3 MB
static constexpr uint32_t OTA_MAX_DURATION_MS = 5UL * 60UL * 1000UL;   // 5 minutes
static constexpr size_t OTA_URL_MAX_LEN = 192;
static constexpr size_t OTA_MD5_MAX_LEN = 32;
