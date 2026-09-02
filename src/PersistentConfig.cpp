// PersistentConfig.cpp
#include "PersistentConfig.h"

nvs_handle_t PersistentConfig::handle = 0;
bool PersistentConfig::initialized = false;

bool PersistentConfig::init()
{
    if (initialized) return true;
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    err = nvs_open("vehicle_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.printf("[NVS] Failed to open: %s\n", esp_err_to_name(err));
        return false;
    }
    
    initialized = true;
    Serial.println("[NVS] Persistent storage initialized");
    return true;
}

bool PersistentConfig::getString(const char* key, char* out, size_t maxLen, const char* defaultVal)
{
    if (!initialized) init();
    
    size_t required_size = maxLen;
    esp_err_t err = nvs_get_str(handle, key, out, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Key doesn't exist — write default and return it
        if (defaultVal) {
            strncpy(out, defaultVal, maxLen - 1);
            out[maxLen - 1] = '\0';
            setString(key, defaultVal); // Save default for next time
        } else {
            out[0] = '\0';
        }
        return true;
    }
    
    if (err != ESP_OK) {
        Serial.printf("[NVS] getString(%s) failed: %s\n", key, esp_err_to_name(err));
        if (defaultVal) {
            strncpy(out, defaultVal, maxLen - 1);
            out[maxLen - 1] = '\0';
        }
        return false;
    }
    
    return true;
}

bool PersistentConfig::setString(const char* key, const char* value)
{
    if (!initialized) init();
    
    esp_err_t err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        Serial.printf("[NVS] setString(%s) failed: %s\n", key, esp_err_to_name(err));
        return false;
    }
    
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        Serial.printf("[NVS] commit failed: %s\n", esp_err_to_name(err));
        return false;
    }
    
    Serial.printf("[NVS] Saved '%s' = '%s'\n", key, value);
    return true;
}

bool PersistentConfig::getInt(const char* key, int32_t* out, int32_t defaultVal)
{
    if (!initialized) init();
    
    esp_err_t err = nvs_get_i32(handle, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = defaultVal;
        setInt(key, defaultVal);
        return true;
    }
    if (err != ESP_OK) {
        *out = defaultVal;
        return false;
    }
    return true;
}

bool PersistentConfig::setInt(const char* key, int32_t value)
{
    if (!initialized) init();
    
    esp_err_t err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK) return false;
    
    err = nvs_commit(handle);
    return err == ESP_OK;
}

// MQTT-specific
bool PersistentConfig::getMqttClientId(char* out, size_t maxLen)
{
    return getString("mqtt_client_id", out, maxLen, "ESP32_Vehicle_Detector");
}

bool PersistentConfig::setMqttClientId(const char* id)
{
    return setString("mqtt_client_id", id);
}