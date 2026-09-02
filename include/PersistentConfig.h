// PersistentConfig.h
#pragma once
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "Config.h"
#include "VehicleDetector.h"
#include "TrafficStats.h"
#include "LoopGeometry.h"
#include "Globals.h"

class PersistentConfig
{
    public:
        static bool init();                          // Call once in setup()
        static bool getString(const char* key, char* out, size_t maxLen, const char* defaultVal);
        static bool setString(const char* key, const char* value);
        static bool getInt(const char* key, int32_t* out, int32_t defaultVal);
        static bool setInt(const char* key, int32_t value);
        static bool setUint(const char* key, uint32_t value);
        // MQTT-specific helpers
        static bool getMqttClientId(char* out, size_t maxLen);
        static bool setMqttClientId(const char* id);
        static bool getMqttServer(char* out, size_t maxLen);
        static bool setMqttServer(const char* server);
        static bool loadConfig(char* clientId, char* server, IPAddress ip,uint16_t* port, char *user, char *pass, char *apn,
                char *mqttTopicEvents, char *mqttTopicCommands, char *mqttTopicCommandResponses) ;
        static bool setConfig(char* clientId, char* server, IPAddress ip,uint16_t* port, char *user, char *pass, char *apn,
                char *mqttTopicEvents, char *mqttTopicCommands, char *mqttTopicCommandResponses) ;

        // Detector configs (8 sensors: 2 sensors x 4 channels)
        static bool saveDetectorConfigs();
        static bool loadDetectorConfigs();
        static bool saveDetectorConfig(uint8_t sensor, uint8_t ch, const DetectorConfig &cfg);
        static bool loadDetectorConfig(uint8_t sensor, uint8_t ch, DetectorConfig &cfg);

        // Loop configs (4 speed pairs)
        static bool saveLoopConfig(uint8_t idx, const LoopConfig &cfg);
        static bool loadLoopConfig(uint8_t idx, LoopConfig &cfg);
        static bool saveAllLoopConfigs();
        static bool loadAllLoopConfigs();

        // Sensor LC tuning parameters
        static bool saveSensorLC(uint8_t sensor, const ChannelLC &lc);
        static bool loadSensorLC(uint8_t sensor, ChannelLC &lc);

        // Traffic rules and report config
        static bool saveTrafficRules();
        static bool loadTrafficRules();
        static bool saveReportConfig();
        static bool loadReportConfig();

        // Loop geometry
        static bool saveLoopGeometry();
        static bool loadLoopGeometry();

        // Convenience
        static bool saveAllConfigs();
        static bool loadAllConfigs();

    private:
        static nvs_handle_t handle;
        static bool initialized;
};