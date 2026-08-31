#pragma once

#include <Arduino.h>
#include "Config.h"

// ============================================================
// Individual Loop Geometry
// ============================================================
struct LoopGeometry
{
    uint8_t sensor_id = 0;
    uint8_t channel_id = 0;
    uint8_t lane_id = 0;

    float x_m = 0.0f;
    float y_m = 0.0f;

    float loop_length_m = 1.8f;
    float loop_width_m = 0.6f;

    bool used_for_speed = false;
    bool used_for_lane_change = false;
    bool used_for_classification = true;

    int adjacent_loops[4] = {-1, -1, -1, -1};
    uint8_t adjacent_count = 0;

    char name[16] = "";

    LoopGeometry() = default;

    LoopGeometry(uint8_t sensor, uint8_t channel, uint8_t lane,
                 float x, float y, const char *loopName = "")
        : sensor_id(sensor), channel_id(channel), lane_id(lane),
          x_m(x), y_m(y)
    {
        if (loopName)
            snprintf(name, sizeof(name), "%s", loopName);
        else
            snprintf(name, sizeof(name), "S%dC%d", sensor + 1, channel);
    }
};

// ============================================================
// Site Geometry Configuration
// ============================================================
struct SiteGeometryConfig
{
    float road_direction_deg = 0.0f;
    uint8_t lane_count = 4;
    float lane_width_m = 3.5f;
    float speed_limit_kmh = 100.0f;
    bool left_to_right_direction = true;

    char site_id[32] = "SITE-01";
};

// ============================================================
// Loop Geometry Manager
// ============================================================
#define MAX_LOOPS 8
#define MAX_ADJACENT_PER_LOOP 4

class LoopGeometryManager
{
public:
    LoopGeometryManager();

    void reset();
    void loadDefaults();

    // Configuration
    void setSiteConfig(const SiteGeometryConfig &cfg);
    SiteGeometryConfig getSiteConfig() const;

    void setLoop(uint8_t index, const LoopGeometry &loop);
    LoopGeometry getLoop(uint8_t index) const;
    uint8_t getLoopCount() const;

    // Query
    const LoopGeometry *findLoop(uint8_t sensor, uint8_t channel) const;
    int findLoopIndex(uint8_t sensor, uint8_t channel) const;
    bool areAdjacent(uint8_t idx1, uint8_t idx2) const;
    bool getAdjacentLoops(uint8_t sensor, uint8_t channel,
                          uint8_t adjSensors[], uint8_t adjChannels[],
                          uint8_t &count) const;

    // Geometry-based adjacency computation
    void computeAdjacency();

    // Serialization
    void buildStatusString(char *buf, size_t bufSize) const;
    bool parseFromCommand(const String &cmd);

    // Lane change parameters (derived from geometry)
    float getLaneWidth() const { return site_cfg_.lane_width_m; }
    float getLoopDistance(uint8_t idx1, uint8_t idx2) const;
    uint8_t getLaneId(uint8_t sensor, uint8_t channel) const;

private:
    SiteGeometryConfig site_cfg_;
    LoopGeometry loops_[MAX_LOOPS];
    uint8_t loop_count_;

    static float distanceBetween(const LoopGeometry &a, const LoopGeometry &b);
};

// Global instance
extern LoopGeometryManager g_loopGeometry;
