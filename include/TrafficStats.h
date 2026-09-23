#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============================================================
// Vehicle class index
// ============================================================
enum VehicleClassIndex
{
    VCLASS_UNKNOWN = 0,
    VCLASS_MOTOR,
    VCLASS_CAR,
    VCLASS_PICKUP,
    VCLASS_VAN,
    VCLASS_BUS,
    VCLASS_TRUCK_S,
    VCLASS_TRUCK_2,
    VCLASS_TRUCK_3,
    VCLASS_TRUCK_4,
    VCLASS_COUNT
};

// ============================================================
// Traffic rules
// ============================================================
struct TrafficRulesConfig
{
    bool enable_speed_violation = false;
    float speed_limit_kmh = 100.0f;
    float speed_tolerance_kmh = 5.0f;

    bool enable_distance_violation = false;
    float min_follow_distance_m = 30.0f;
    float min_headway_s = 1.2f;
    uint32_t max_headway_ms = 20000;

    float assume_speed_kmh = 0.0f;

    bool enable_lane_violation = false;
    uint32_t min_straddle_overlap_ms = 100;
    float min_straddle_overlap_ratio = 0.25f;
    uint32_t lane_violation_cooldown_ms = 2000;
};

// ============================================================
// Periodic report config
// ============================================================
struct ReportConfig
{
    bool enabled = true;
    uint32_t interval_ms = 5 * 60 * 1000;
    bool periodic_clear = true;
};

// ============================================================
// Per-class stats
// ============================================================
struct ClassStat
{
    uint32_t count = 0;
    uint32_t speed_samples = 0;
    double speed_sum = 0.0;

    uint32_t speed_violations = 0;
    uint32_t distance_violations = 0;
    uint32_t lane_violations = 0;
};

// ============================================================
// Aggregate stats
// ============================================================
struct TrafficStatistics
{
    uint64_t window_start_us = 0;

    uint32_t total_vehicles = 0;

    uint32_t speed_samples = 0;
    double speed_sum = 0.0;

    uint32_t speed_violations = 0;
    uint32_t distance_violations = 0;
    uint32_t lane_violations = 0;

    ClassStat cls[VCLASS_COUNT];
};

extern SemaphoreHandle_t statsMutex;
extern TrafficStatistics g_traffic_stats;
extern ReportConfig g_report_cfg;
extern TrafficRulesConfig g_traffic_rules;

void trafficStatsInit();
void trafficStatsReset();

int vehicleClassIndexFromString(const char *s);
const char *vehicleClassIndexToString(int idx);

void trafficStatsRecordVehicle(int cls, float speed_kmh);
void trafficStatsRecordSpeedViolation(int cls, float speed_kmh);
void trafficStatsRecordDistanceViolation(int cls);
void trafficStatsRecordLaneViolation(int cls);

void trafficStatsSetReportSender(void (*fn)(const char *));
void trafficStatsPeriodic();

String trafficStatsBuildReport(bool clear_after);
