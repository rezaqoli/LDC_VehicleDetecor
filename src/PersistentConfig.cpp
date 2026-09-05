// PersistentConfig.cpp
#include "PersistentConfig.h"
#include <cstring>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern IPAddress mqttServerIp;

nvs_handle_t PersistentConfig::handle = 0;
bool PersistentConfig::initialized = false;

bool PersistentConfig::init()
{
    if (initialized)
        return true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open("vehicle_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] Failed to open: %s\n", esp_err_to_name(err));
        return false;
    }

    initialized = true;
    Serial.println("[NVS] Persistent storage initialized");
    return true;
}

bool PersistentConfig::getString(const char *key, char *out, size_t maxLen, const char *defaultVal)
{
    if (!initialized)
        init();

    size_t required_size = maxLen;
    esp_err_t err = nvs_get_str(handle, key, out, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        // Key doesn't exist — write default and return it
        if (defaultVal)
        {
            strncpy(out, defaultVal, maxLen - 1);
            out[maxLen - 1] = '\0';
            setString(key, defaultVal); // Save default for next time
        }
        else
        {
            out[0] = '\0';
        }
        return true;
    }

    if (err != ESP_OK)
    {
        Serial.printf("[NVS] getString(%s) failed: %s\n", key, esp_err_to_name(err));
        if (defaultVal)
        {
            strncpy(out, defaultVal, maxLen - 1);
            out[maxLen - 1] = '\0';
        }
        return false;
    }

    return true;
}

bool PersistentConfig::setString(const char *key, const char *value)
{
    if (!initialized)
        init();

    esp_err_t err = nvs_set_str(handle, key, value);
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] setString(%s) failed: %s\n", key, esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] commit failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[NVS] Saved '%s' = '%s'\n", key, value);
    return true;
}

bool PersistentConfig::getInt(const char *key, int32_t *out, int32_t defaultVal)
{
    if (!initialized)
        init();

    esp_err_t err = nvs_get_i32(handle, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *out = defaultVal;
        setInt(key, defaultVal);
        return true;
    }
    if (err != ESP_OK)
    {
        *out = defaultVal;
        return false;
    }
    return true;
}

bool PersistentConfig::setInt(const char *key, int32_t value)
{
    if (!initialized)
        init();

    esp_err_t err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK)
        return false;

    err = nvs_commit(handle);
    return err == ESP_OK;
}

bool PersistentConfig::setUint(const char *key, uint32_t value)
{
    if (!initialized)
        init();

    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK)
        return false;

    err = nvs_commit(handle);
    return err == ESP_OK;
}

bool PersistentConfig::getUint(const char *key, uint32_t *out, uint32_t defaultVal)
{
    if (!initialized)
        init();

    esp_err_t err = nvs_get_u32(handle, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *out = defaultVal;
        setUint(key, defaultVal);
        return true;
    }
    if (err != ESP_OK)
    {
        *out = defaultVal;
        return false;
    }
    return true;
}

// MQTT-specific
bool PersistentConfig::getMqttClientId(char *out, size_t maxLen)
{
    return getString("mqtt_client_id", out, maxLen, "ESP32_Vehicle_Detector");
}

bool PersistentConfig::setMqttClientId(const char *id)
{
    return setString("mqtt_client_id", id);
}

bool PersistentConfig::getMqttServer(char *out, size_t maxLen)
{
    return getString("mqtt_server", out, maxLen, "\0");
}
bool PersistentConfig::setMqttServer(const char *server)
{
    return setString("mqtt_server", server);
}

bool PersistentConfig::setConfig(char *clientId, char *server, IPAddress ip, uint16_t *port, char *user, char *pass,
                                 char *apn,
                                 char *mqttTopicEvents, char *mqttTopicCommands, char *mqttTopicCommandResponses)
{
    if(!initialized)
        init();
    bool success = true;
    if(!clientId || !server || !port || !user || !pass || !apn || !mqttTopicEvents || !mqttTopicCommands || !mqttTopicCommandResponses)
    {
        Serial.println("[NVS] setConfig: Invalid parameters");
        return false;
    }
    if( clientId != nullptr)
    {
       success &= setString("mqtt_client_id", clientId);
    }
    if( server != nullptr)
    {
       success &= setString("mqtt_server", server);
    }
    if(ip != 0)
    {
       success &= setUint("ip_server", ip);
    }
    if(port != nullptr)
    {
       success &= setInt("mqtt_port", *port);
    }
    if( user != nullptr)
    {
       success &= setString("mqtt_user", user);
    }
    if( pass != nullptr)
    {
       success &= setString("mqtt_pass", pass);
    }
    if( apn != nullptr)
    {
       success &= setString("lte_apn", apn);
    }
    if( mqttTopicEvents != nullptr)
    {
       //success &= setString("mqtt_topic_events", mqttTopicEvents);
       success &= setString("events", mqttTopicEvents);
    }
    if ( mqttTopicCommands != nullptr)
    {
       //success &= setString("mqtt_topic_commands", mqttTopicCommands);
       success &= setString("commands", mqttTopicCommands);
    }
    if ( mqttTopicCommandResponses != nullptr)
    {
       //success &= setString("mqtt_topic_command_responses", mqttTopicCommandResponses);
       success &= setString("cmnds_resp", mqttTopicCommandResponses);
    }
    return success;
}


bool PersistentConfig::loadConfig(char *clientId, char *server, IPAddress ip, uint16_t *port, char *user, char *pass,
                                  char *apn,
                                  char *mqttTopicEvents, char *mqttTopicCommands, char *mqttTopicCommandResponses)
{
    getString("mqtt_client_id", clientId, 32, "ESP32_Vehicle_Detector");
    getString("mqtt_server", server, 64, "iot.iolink.ir");
    getUint("ip_server", (uint32_t *)&ip, 0);
    getInt("mqtt_port", (int32_t *)port, 1883);
    getString("mqtt_user", user, 32, "");
    getString("mqtt_pass", pass, 32, "");
    getString("lte_apn", apn, 32, "shatelmobile");
    getString("events", mqttTopicEvents, 64, "vehicles/events");
    getString("commands", mqttTopicCommands, 64, "vehicles/commands");
    getString("cmnds_resp", mqttTopicCommandResponses, 64, "vehicles/command_responses");
    return true;
}

// ============================================================
// Detector config persistence (binary blob per sensor/channel)
// ============================================================
bool PersistentConfig::saveDetectorConfig(uint8_t sensor, uint8_t ch, const DetectorConfig &cfg)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "det_%u_%u", sensor, ch);

    esp_err_t err = nvs_set_blob(handle, key, &cfg, sizeof(cfg));
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] saveDetectorConfig(%u,%u) failed: %s\n", sensor, ch, esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] commit failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[NVS] Saved detector config sensor=%u ch=%u\n", sensor, ch);
    return true;
}

bool PersistentConfig::loadDetectorConfig(uint8_t sensor, uint8_t ch, DetectorConfig &cfg)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "det_%u_%u", sensor, ch);

    size_t required_size = sizeof(cfg);
    esp_err_t err = nvs_get_blob(handle, key, &cfg, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        // Use default and save
        memset(&cfg, 0, sizeof(cfg));
        cfg.enter_thresh = 0.0008f;
        cfg.exit_diff_th = 50.0f;
        cfg.exit_ratio = 0.25f;
        cfg.exit_hysteresis_cnt = 5;
        cfg.absolute_min_dev = 0.0003f;
        cfg.min_event_ms = 20;
        cfg.max_event_ms = 5000;
        cfg.motor_max_len = 2.4f;
        cfg.car_max_len = 4.0f;
        cfg.pickup_max_len = 5.0f;
        cfg.van_max_len = 6.5f;
        cfg.bus_max_len = 11.0f;
        cfg.truck_s_max_len = 8.0f;
        cfg.truck_2_max_len = 10.5f;
        cfg.truck_3_max_len = 14.0f;
        cfg.truck_4_plus_min_len = 14.0f;
        cfg.classify_peak_to_thresh_low = 2.5f;
        cfg.classify_peak_to_thresh_high = 8.0f;
        cfg.classify_rise_short_ms = 45.0f;
        cfg.classify_rise_mid_ms = 160.0f;
        cfg.classify_rise_long_ms = 300.0f;
        cfg.classify_energy_low = 0.00001f;
        cfg.classify_energy_mid = 0.00008f;
        cfg.classify_energy_high = 0.00020f;
        cfg.classify_crest_spiky = 2.8f;
        cfg.classify_crest_broad = 1.6f;
        cfg.classify_skew_tol = 0.35f;
        cfg.classify_skew_high = 0.75f;
        cfg.classify_com_center_min = 0.38f;
        cfg.classify_com_center_max = 0.62f;
        cfg.classify_com_edge_min = 0.32f;
        cfg.classify_com_edge_max = 0.68f;
        cfg.classify_width_medium = 0.20f;
        cfg.classify_width_wide = 0.45f;
        cfg.classify_std_peak_high = 0.35f;
        cfg.classify_std_peak_low = 0.18f;
        cfg.peak_prominence_ratio = 0.35f;
        cfg.min_axle_distance_ms = 80.0f;
        cfg.min_entry_slope = 0.0005f;
        cfg.min_slow_enter_ms = 80;
        cfg.peak_prominence_abs = 0.0002f;
        cfg.calib_samples = 640;
        cfg.warmup_samples = 50;
        cfg.baseline_alpha = 0.0004f;
        cfg.confirm_samples = 3;
        cfg.min_event_samples = 6;
        cfg.smoothing_alpha = 0.35f;
        cfg.peak_to_baseline_ratio = 1.8f;
        cfg.enter_hysteresis_ratio = 0.70f;
        cfg.exit_hysteresis_ratio = 0.55f;
        cfg.auto_threshold = true;
        cfg.enter_sigma = 5.0f;
        cfg.abs_sigma = 2.5f;
        cfg.min_enter_thresh = 0.0008f;
        cfg.min_abs_dev = 0.0003f;
        cfg.max_enter_thresh = 1.0f;
        cfg.max_abs_dev = 1.0f;
        cfg.default_speed_kmh = 90.0f;

        saveDetectorConfig(sensor, ch, cfg);
        return true;
    }

    if (err != ESP_OK)
    {
        Serial.printf("[NVS] loadDetectorConfig(%u,%u) failed: %s\n", sensor, ch, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool PersistentConfig::saveDetectorConfigs()
{
    bool success = true;
    for (int s = 0; s < 2; s++)
        for (int ch = 0; ch < 4; ch++)
            success &= saveDetectorConfig(s, ch, det[s][ch].getConfig());
    return success;
}

bool PersistentConfig::loadDetectorConfigs()
{
    bool success = true;
    for (int s = 0; s < 2; s++)
        for (int ch = 0; ch < 4; ch++)
        {
            DetectorConfig cfg = det[s][ch].getConfig();
            success &= loadDetectorConfig(s, ch, cfg);
        }
    return success;
}

// ============================================================
// Sensor LC tuning parameters persistence (binary blob per sensor)
// ============================================================
bool PersistentConfig::saveSensorLC(uint8_t sensor, const ChannelLC &lc)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "lc_%u", sensor);

    esp_err_t err = nvs_set_blob(handle, key, &lc, sizeof(lc));
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] saveSensorLC(%u) failed: %s\n", sensor, esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        Serial.printf("[NVS] commit failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[NVS] Saved LC sensor=%u\n", sensor);
    return true;
}

bool PersistentConfig::loadSensorLC(uint8_t sensor, ChannelLC &lc)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "lc_%u", sensor);

    size_t required_size = sizeof(lc);
    esp_err_t err = nvs_get_blob(handle, key, &lc, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        // Use defaults
        memset(&lc, 0, sizeof(lc));
        for (int i = 0; i < 4; i++)
        {
            lc.L[i] = 13.0f;
            lc.C[i] = 330.0f;
            lc.conversion_time[i] = 0x9C40;
            lc.driver_current[i] = 0xA000;
        }
        saveSensorLC(sensor, lc);
        return true;
    }

    if (err != ESP_OK)
    {
        Serial.printf("[NVS] loadSensorLC(%u) failed: %s\n", sensor, esp_err_to_name(err));
        return false;
    }

    return true;
}

// ============================================================
// Loop config persistence (string serialization per index)
// ============================================================
bool PersistentConfig::saveLoopConfig(uint8_t idx, const LoopConfig &cfg)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "loopcfg_%u", idx);

    char buf[128];
    snprintf(buf, sizeof(buf), "%d|%.2f|%u|%u|%u|%u",
             cfg.dualLoop ? 1 : 0, cfg.distance,
             cfg.sensor1, cfg.ch1, cfg.sensor2, cfg.ch2);

    return setString(key, buf);
}

bool PersistentConfig::loadLoopConfig(uint8_t idx, LoopConfig &cfg)
{
    if (!initialized)
        init();

    char key[16];
    snprintf(key, sizeof(key), "loopcfg_%u", idx);

    char buf[128] = "";
    getString(key, buf, sizeof(buf), "");

    if (buf[0] == '\0')
    {
        // Use defaults
        cfg = LoopConfig{false, 0.4f, 0, 0, 1, 0};
        saveLoopConfig(idx, cfg);
        return true;
    }

    // Parse pipe-delimited: dualLoop|distance|sensor1|ch1|sensor2|ch2
    float p[6] = {};
    int cnt = 0;
    char *token = strtok(buf, "|");

    while (token != NULL && cnt < 6)
    {
        p[cnt++] = atof(token);
        token = strtok(NULL, "|");
    }

    if (cnt >= 6)
    {
        cfg.dualLoop = (p[0] != 0);
        cfg.distance = p[1];
        cfg.sensor1  = (uint8_t)constrain((int)p[2], 0, 1);
        cfg.ch1      = (uint8_t)constrain((int)p[3], 0, 3);
        cfg.sensor2  = (uint8_t)constrain((int)p[4], 0, 1);
        cfg.ch2      = (uint8_t)constrain((int)p[5], 0, 3);
    }
    else
    {
        cfg = LoopConfig{false, 0.4f, 0, 0, 1, 0};
    }

    return true;
}

bool PersistentConfig::saveAllLoopConfigs()
{
    bool success = true;
    for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
        success &= saveLoopConfig(i, loopCfg[i]);
    return success;
}

bool PersistentConfig::loadAllLoopConfigs()
{
    bool success = true;
    for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
        success &= loadLoopConfig(i, loopCfg[i]);
    return success;
}

// ============================================================
// Traffic rules and report config persistence (string serialization)
// ============================================================
bool PersistentConfig::saveTrafficRules()
{
    if (!initialized)
        init();

    static char buf[512];
    snprintf(buf, sizeof(buf),
             "%d|%.1f|%.1f|%d|%.1f|%.1f|%lu|%.1f|%d|%lu|%.2f|%lu",
             g_traffic_rules.enable_speed_violation ? 1 : 0,
             g_traffic_rules.speed_limit_kmh,
             g_traffic_rules.speed_tolerance_kmh,
             g_traffic_rules.enable_distance_violation ? 1 : 0,
             g_traffic_rules.min_follow_distance_m,
             g_traffic_rules.min_headway_s,
             (unsigned long)g_traffic_rules.max_headway_ms,
             g_traffic_rules.assume_speed_kmh,
             g_traffic_rules.enable_lane_violation ? 1 : 0,
             (unsigned long)g_traffic_rules.min_straddle_overlap_ms,
             g_traffic_rules.min_straddle_overlap_ratio,
             (unsigned long)g_traffic_rules.lane_violation_cooldown_ms);

    return setString("traffic_rules", buf);
}

bool PersistentConfig::loadTrafficRules()
{
    if (!initialized)
        init();

    static char buf[512];
    buf[0] = '\0';
    getString("traffic_rules", buf, sizeof(buf), "");

    if (buf[0] == '\0')
    {
        saveTrafficRules();
        return true;
    }

    float p[12] = {};
    int cnt = 0;
    char *token = strtok(buf, "|");

    while (token != NULL && cnt < 12)
    {
        p[cnt++] = atof(token);
        token = strtok(NULL, "|");
    }

    if (cnt >= 12)
    {
        g_traffic_rules.enable_speed_violation = (p[0] != 0);
        g_traffic_rules.speed_limit_kmh = p[1];
        g_traffic_rules.speed_tolerance_kmh = p[2];
        g_traffic_rules.enable_distance_violation = (p[3] != 0);
        g_traffic_rules.min_follow_distance_m = p[4];
        g_traffic_rules.min_headway_s = p[5];
        g_traffic_rules.max_headway_ms = (uint32_t)p[6];
        g_traffic_rules.assume_speed_kmh = p[7];
        g_traffic_rules.enable_lane_violation = (p[8] != 0);
        g_traffic_rules.min_straddle_overlap_ms = (uint32_t)p[9];
        g_traffic_rules.min_straddle_overlap_ratio = p[10];
        g_traffic_rules.lane_violation_cooldown_ms = (uint32_t)p[11];
    }

    return true;
}

bool PersistentConfig::saveReportConfig()
{
    if (!initialized)
        init();

    char buf[128];
    snprintf(buf, sizeof(buf), "%d|%lu|%d",
             g_report_cfg.enabled ? 1 : 0,
             (unsigned long)g_report_cfg.interval_ms,
             g_report_cfg.periodic_clear ? 1 : 0);

    return setString("report_cfg", buf);
}

bool PersistentConfig::loadReportConfig()
{
    if (!initialized)
        init();

    char buf[128] = "";
    getString("report_cfg", buf, sizeof(buf), "");

    if (buf[0] == '\0')
    {
        saveReportConfig();
        return true;
    }

    float p[3] = {};
    int cnt = 0;
    const char *ptr = buf;

    while (*ptr && cnt < 3)
    {
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        char *endptr;
        p[cnt++] = strtof(ptr, &endptr);
        ptr = endptr;
        while (*ptr == '|' || *ptr == ' ' || *ptr == '\t') ptr++;
    }

    if (cnt >= 3)
    {
        g_report_cfg.enabled = (p[0] != 0);
        g_report_cfg.interval_ms = (uint32_t)p[1];
        g_report_cfg.periodic_clear = (p[2] != 0);
    }

    return true;
}
// ============================================================
// Loop geometry persistence (string serialization)
// ============================================================
bool PersistentConfig::saveLoopGeometry()
{
    if (!initialized)
        init();

    static char buf[2048];
    buf[0] = '\0';
    SiteGeometryConfig sc = g_loopGeometry.getSiteConfig();
    snprintf(buf, sizeof(buf),
             "%s|%u|%.1f|%.0f|%.0f|%u|%u",
             sc.site_id,
             sc.lane_count,
             sc.lane_width_m,
             sc.speed_limit_kmh,
             sc.road_direction_deg,
             sc.left_to_right_direction ? 1 : 0,
             g_loopGeometry.getLoopCount());

    // Append each loop's data
    for (uint8_t i = 0; i < g_loopGeometry.getLoopCount(); i++)
    {
        LoopGeometry lg = g_loopGeometry.getLoop(i);
        char loop_buf[256];
        snprintf(loop_buf, sizeof(loop_buf),
                 "|%u|%u|%u|%.1f|%.1f|%u|%u|%u|%d|%d|%d|%d",
                 lg.sensor_id, lg.channel_id, lg.lane_id,
                 lg.x_m, lg.y_m, lg.used_for_speed,
                 lg.used_for_lane_change, lg.used_for_classification,
                 lg.adjacent_loops[0], lg.adjacent_loops[1],
                 lg.adjacent_loops[2], lg.adjacent_loops[3]);
        strncat(buf, loop_buf, sizeof(buf) - strlen(buf) - 1);
    }

    return setString("loop_geometry", buf);
}

bool PersistentConfig::loadLoopGeometry()
{
    if (!initialized)
        init();

    static char buf[2048];
    buf[0] = '\0';
    getString("loop_geometry", buf, sizeof(buf), "");

    if (buf[0] == '\0')
    {
        g_loopGeometry.loadDefaults();
        saveLoopGeometry();
        return true;
    }

    // Tokenize the buffer
    static const int MAX_TOKENS = 256;
    static const char *tokens[MAX_TOKENS];
    int tokenCount = 0;

    static char tmp[2048];
    strncpy(tmp, buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(tmp, "|", &saveptr);
    while (tok && tokenCount < MAX_TOKENS)
    {
        tokens[tokenCount++] = tok;
        tok = strtok_r(NULL, "|", &saveptr);
    }

    if (tokenCount < 7)
    {
        g_loopGeometry.loadDefaults();
        saveLoopGeometry();
        return true;
    }

    // Parse site config from tokens first (into local vars, not into g_loopGeometry yet)
    char site_id[32];
    snprintf(site_id, sizeof(site_id), "%s", tokens[0]);
    uint8_t lane_count = (uint8_t)atoi(tokens[1]);
    float lane_width_m = (float)atof(tokens[2]);
    float speed_limit_kmh = (float)atof(tokens[3]);
    float road_direction_deg = (float)atof(tokens[4]);
    bool left_to_right = (atoi(tokens[5]) != 0);
    uint8_t loop_count = (uint8_t)atoi(tokens[6]);

    // NOW reset and fill — order matters!
    g_loopGeometry.reset();

    // FIX: Use reference (&) to modify the actual internal config
    SiteGeometryConfig &sc = g_loopGeometry.getSiteConfig();
    snprintf(sc.site_id, sizeof(sc.site_id), "%s", site_id);
    sc.lane_count = lane_count;
    sc.lane_width_m = lane_width_m;
    sc.speed_limit_kmh = speed_limit_kmh;
    sc.road_direction_deg = road_direction_deg;
    sc.left_to_right_direction = left_to_right;

    // Each loop has 12 fields
    int pos = 7;
    for (uint8_t i = 0; i < loop_count && (pos + 12) <= tokenCount; i++)
    {
        LoopGeometry lg;
        lg.sensor_id = (uint8_t)atoi(tokens[pos + 0]);
        lg.channel_id = (uint8_t)atoi(tokens[pos + 1]);
        lg.lane_id = (uint8_t)atoi(tokens[pos + 2]);
        lg.x_m = (float)atof(tokens[pos + 3]);
        lg.y_m = (float)atof(tokens[pos + 4]);
        lg.used_for_speed = (atoi(tokens[pos + 5]) != 0);
        lg.used_for_lane_change = (atoi(tokens[pos + 6]) != 0);
        lg.used_for_classification = (atoi(tokens[pos + 7]) != 0);
        lg.adjacent_loops[0] = atoi(tokens[pos + 8]);
        lg.adjacent_loops[1] = atoi(tokens[pos + 9]);
        lg.adjacent_loops[2] = atoi(tokens[pos + 10]);
        lg.adjacent_loops[3] = atoi(tokens[pos + 11]);
        lg.adjacent_count = 0;  // Will be computed by setLoop or later

        g_loopGeometry.setLoop(i, lg);
        pos += 12;
    }

    // No need to re-set left_to_right — already done above
    // Remove the duplicate line that was in the original

    // Don't call saveLoopGeometry() here — you just LOADED from NVS, no need to write back
    // Only save if you modified something (which you didn't — you loaded)

    return true;
}

// ============================================================
// Convenience methods
// ============================================================
bool PersistentConfig::saveAllConfigs()
{
    if (!initialized)
        init();

    bool success = true;
    success &= saveDetectorConfigs();
    success &= saveAllLoopConfigs();
    success &= saveSensorLC(0, sensor1LC);
    success &= saveSensorLC(1, sensor2LC);
    success &= saveTrafficRules();
    success &= saveReportConfig();
    success &= saveLoopGeometry();
    return success;
}

bool PersistentConfig::loadAllConfigs()
{
    if (!initialized)
        init();

    bool success = true;
    success &= loadDetectorConfigs();
    success &= loadAllLoopConfigs();
    success &= loadSensorLC(0, sensor1LC);
    success &= loadSensorLC(1, sensor2LC);
    success &= loadTrafficRules();
    success &= loadReportConfig();
    success &= loadLoopGeometry();
    success &= loadConfig(mqttClientId, mqttServer, mqttServerIp, &mqttPort, mqttUser, mqttPass, lte_apn,
                           mqttTopicEvents, mqttTopicCommands, mqttTopicCommandResponses);
    return success;
}