// PersistentConfig.h
#pragma once
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>

class PersistentConfig {
public:
    static bool init();                          // Call once in setup()
    static bool getString(const char* key, char* out, size_t maxLen, const char* defaultVal);
    static bool setString(const char* key, const char* value);
    static bool getInt(const char* key, int32_t* out, int32_t defaultVal);
    static bool setInt(const char* key, int32_t value);
    
    // MQTT-specific helpers
    static bool getMqttClientId(char* out, size_t maxLen);
    static bool setMqttClientId(const char* id);
    
private:
    static nvs_handle_t handle;
    static bool initialized;
};