#include "TrafficStats.h"
#include "esp_timer.h"
#include <cstring>
#include <ctime>

SemaphoreHandle_t statsMutex = nullptr;
TrafficStatistics g_traffic_stats;
ReportConfig g_report_cfg;
TrafficRulesConfig g_traffic_rules;

static void (*reportSender)(const char *) = nullptr;

static const char *classNames[VCLASS_COUNT] =
{
    "Unknown",
    "Motor",
    "Car",
    "Pickup",
    "Van",
    "Bus",
    "TruckS",
    "Truck2",
    "Truck3",
    "Truck4+"
};

// ============================================================
// Init / Reset
// ============================================================
static void trafficStatsResetUnlocked()
{
    memset(&g_traffic_stats, 0, sizeof(g_traffic_stats));
    g_traffic_stats.window_start_us = esp_timer_get_time();
}

void trafficStatsInit()
{
    if (!statsMutex)
        statsMutex = xSemaphoreCreateMutex();

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        trafficStatsResetUnlocked();
        xSemaphoreGive(statsMutex);
    }
}

void trafficStatsReset()
{
    if (!statsMutex)
        return;

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        trafficStatsResetUnlocked();
        xSemaphoreGive(statsMutex);
    }
}

// ============================================================
// Class helpers
// ============================================================
int vehicleClassIndexFromString(const char *s)
{
    if (!s || !s[0])
        return VCLASS_UNKNOWN;

    for (int i = 0; i < VCLASS_COUNT; i++)
    {
        if (strcmp(s, classNames[i]) == 0)
            return i;
    }

    return VCLASS_UNKNOWN;
}

const char *vehicleClassIndexToString(int idx)
{
    if (idx < 0 || idx >= VCLASS_COUNT)
        return "Unknown";

    return classNames[idx];
}

// ============================================================
// Record functions
// ============================================================
void trafficStatsRecordVehicle(int cls, float speed_kmh)
{
    if (!statsMutex)
        return;

    if (cls < 0 || cls >= VCLASS_COUNT)
        cls = VCLASS_UNKNOWN;

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_traffic_stats.total_vehicles++;
        g_traffic_stats.cls[cls].count++;

        if (speed_kmh > 1.0f)
        {
            g_traffic_stats.speed_samples++;
            g_traffic_stats.speed_sum += speed_kmh;

            g_traffic_stats.cls[cls].speed_samples++;
            g_traffic_stats.cls[cls].speed_sum += speed_kmh;
        }

        xSemaphoreGive(statsMutex);
    }
}

void trafficStatsRecordSpeedViolation(int cls, float speed_kmh)
{
    if (!statsMutex || !g_traffic_rules.enable_speed_violation)
        return;

    if (cls < 0 || cls >= VCLASS_COUNT)
        cls = VCLASS_UNKNOWN;

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_traffic_stats.speed_violations++;
        g_traffic_stats.cls[cls].speed_violations++;
        xSemaphoreGive(statsMutex);
    }
}

void trafficStatsRecordDistanceViolation(int cls)
{
    if (!statsMutex || !g_traffic_rules.enable_distance_violation)
        return;

    if (cls < 0 || cls >= VCLASS_COUNT)
        cls = VCLASS_UNKNOWN;

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_traffic_stats.distance_violations++;
        g_traffic_stats.cls[cls].distance_violations++;
        xSemaphoreGive(statsMutex);
    }
}

void trafficStatsRecordLaneViolation(int cls)
{
    if (!statsMutex || !g_traffic_rules.enable_lane_violation)
        return;

    if (cls < 0 || cls >= VCLASS_COUNT)
        cls = VCLASS_UNKNOWN;

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_traffic_stats.lane_violations++;
        g_traffic_stats.cls[cls].lane_violations++;
        xSemaphoreGive(statsMutex);
    }
}

// ============================================================
// Report sender
// ============================================================
void trafficStatsSetReportSender(void (*fn)(const char *))
{
    reportSender = fn;
}

// ============================================================
// Build report
// ============================================================
String trafficStatsBuildReport(bool clear_after)
{
    if (!statsMutex)
        return String("ERROR|STATS_NOT_READY");

    TrafficStatistics snapshot;
    uint64_t now_us = esp_timer_get_time();

    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return String("ERROR|STATS_BUSY");

    snapshot = g_traffic_stats;

    if (clear_after)
        trafficStatsResetUnlocked();

    xSemaphoreGive(statsMutex);

    uint32_t dur_s = 0;
    if (now_us > snapshot.window_start_us)
        dur_s = (uint32_t)((now_us - snapshot.window_start_us) / 1000000ULL);

    float avg_all = 0.0f;
    if (snapshot.speed_samples > 0)
        avg_all = (float)(snapshot.speed_sum / snapshot.speed_samples);

    String out;
    out.reserve(1800);

    out += "TRAFFIC_REPORT";

    // Optional ISO timestamp prefix if system time is synced.
    {
        char tsBuf[40] = "";
        // Avoid pulling the full TimeManager API here — use settimeofday
        // already applied by taskTimeSync, and only need a quick localtime.
        time_t now = time(nullptr);
        if (now > 1609459200)
        {
            struct tm ti;
            localtime_r(&now, &ti);
            strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%S%z", &ti);
            out += "|ts:";
            out += tsBuf;
        }
    }

    out += "|dur_s:";
    out += String(dur_s);
    out += "|total:";
    out += String(snapshot.total_vehicles);
    out += "|avg_speed:";
    out += String(avg_all, 1);
    out += "|speed_viol:";
    out += String(snapshot.speed_violations);
    out += "|dist_viol:";
    out += String(snapshot.distance_violations);
    out += "|lane_viol:";
    out += String(snapshot.lane_violations);

    for (int i = 0; i < VCLASS_COUNT; i++)
    {
        const char *name = vehicleClassIndexToString(i);

        float class_avg = 0.0f;
        if (snapshot.cls[i].speed_samples > 0)
            class_avg = (float)(snapshot.cls[i].speed_sum / snapshot.cls[i].speed_samples);

        out += "|";
        out += name;
        out += "_cnt:";
        out += String(snapshot.cls[i].count);

        out += "|";
        out += name;
        out += "_avg:";
        out += String(class_avg, 1);
    }

    return out;
}

// ============================================================
// Periodic report
// ============================================================
void trafficStatsPeriodic()
{
    static uint64_t last_report_us = 0;

    if (!g_report_cfg.enabled || g_report_cfg.interval_ms == 0)
        return;

    uint64_t now_us = esp_timer_get_time();

    if (last_report_us == 0)
        last_report_us = now_us;

    uint64_t interval_us = (uint64_t)g_report_cfg.interval_ms * 1000ULL;

    if ((now_us - last_report_us) >= interval_us)
    {
        last_report_us = now_us;

        String report = trafficStatsBuildReport(g_report_cfg.periodic_clear);

        if (reportSender)
            reportSender(report.c_str());
    }
}
