#include "TrafficMonitor.h"
#include "TrafficStats.h"
#include "LoopGeometry.h"
#include "Globals.h"
#include "esp_timer.h"
#include <cstring>
#include <ctype.h>
#include <stdio.h>

// ============================================================
// Helpers
// ============================================================
static bool parseChannelId(const char *id, uint8_t &s, uint8_t &ch)
{
    if (!id)
        return false;

    int sensor = -1;
    int channel = -1;

    for (int i = 0; id[i] != '\0' && i < 12; i++)
    {
        if ((id[i] == 'S' || id[i] == 's') && isdigit((unsigned char)id[i + 1]))
            sensor = (id[i + 1] - '0') - 1; // S1/S2 map to internal 0/1.

        if ((id[i] == 'C' || id[i] == 'c') && isdigit((unsigned char)id[i + 1]))
            channel = id[i + 1] - '0';
    }

    if (sensor < 0 || sensor >= 2)
        return false;

    if (channel < 0 || channel >= 4)
        return false;

    s = (uint8_t)sensor;
    ch = (uint8_t)channel;
    return true;
}

// ============================================================
// Recent events for lane-straddle detection
// ============================================================
struct RecentEventMini
{
    bool valid = false;
    uint32_t start_us = 0;
    uint32_t end_us = 0;
    float duration_ms = 0.0f;
    int cls = VCLASS_UNKNOWN;
};

static RecentEventMini lastEvent[2][4];

// ============================================================
// Adjacent loops for lane-straddle / between-lines detection
// ============================================================
struct AdjacentPair
{
    bool enabled = true;
    uint8_t s1 = 0;
    uint8_t ch1 = 0;
    uint8_t s2 = 0;
    uint8_t ch2 = 0;
    uint64_t last_violation_us = 0;

    AdjacentPair() = default;
    AdjacentPair(bool en, uint8_t _s1, uint8_t _ch1, uint8_t _s2, uint8_t _ch2)
        : enabled(en), s1(_s1), ch1(_ch1), s2(_s2), ch2(_ch2), last_violation_us(0) {}
};

static AdjacentPair adjPairs[] =
{
    AdjacentPair(true, 0, 0, 0, 1),
    AdjacentPair(true, 0, 1, 0, 2),
    AdjacentPair(true, 0, 2, 0, 3),

    AdjacentPair(true, 1, 0, 1, 1),
    AdjacentPair(true, 1, 1, 1, 2),
    AdjacentPair(true, 1, 2, 1, 3)
};

static const int ADJ_PAIR_COUNT = sizeof(adjPairs) / sizeof(adjPairs[0]);

// ============================================================
// Following-distance state
// ============================================================
struct FollowState
{
    bool valid = false;
    uint32_t ref_end_us = 0;
    float speed_ms = 0.0f;
    int cls = VCLASS_UNKNOWN;
};

static FollowState pairFollowState[SPEED_PAIR_COUNT];
static FollowState channelFollowState[2][4];

// Completed speed matches retained long enough to pair two adjacent lanes.
// Values are copied because the detector event-pool slots are released by the
// speed task immediately after processing a match.
struct BetweenLinesMatch
{
    bool valid = false;
    uint8_t pair_idx = 0;
    uint8_t s1 = 0, ch1 = 0, s2 = 0, ch2 = 0;
    uint32_t start_us = 0, end_us = 0;
    float duration_ms = 0.0f;
    float length_m = 0.0f;
    float speed_kmh = 0.0f;
    int cls = VCLASS_UNKNOWN;
};

static BetweenLinesMatch betweenLinesMatches[SPEED_PAIR_COUNT];

static bool intervalsOverlap(const BetweenLinesMatch &a, const BetweenLinesMatch &b,
                             uint32_t &overlap_ms, float &overlap_ratio)
{
    uint32_t start_max = a.start_us > b.start_us ? a.start_us : b.start_us;
    uint32_t end_min = a.end_us < b.end_us ? a.end_us : b.end_us;
    if (end_min <= start_max)
        return false;

    overlap_ms = (end_min - start_max) / 1000U;
    float min_duration = a.duration_ms < b.duration_ms ? a.duration_ms : b.duration_ms;
    overlap_ratio = min_duration > 10.0f ? (float)overlap_ms / min_duration : 0.0f;
    return overlap_ms >= g_traffic_rules.min_straddle_overlap_ms &&
           overlap_ratio >= g_traffic_rules.min_straddle_overlap_ratio;
}

static bool speedPairsAreAdjacent(const LoopConfig &a, const LoopConfig &b)
{
    // The two upstream loops and the two downstream loops must each be
    // adjacent in the persisted geometry. This makes the grouping work for
    // arbitrary sensor/channel layouts instead of hard-coding S1C0..S1C3.
    int au = g_loopGeometry.findLoopIndex(a.sensor1, a.ch1);
    int bu = g_loopGeometry.findLoopIndex(b.sensor1, b.ch1);
    int ad = g_loopGeometry.findLoopIndex(a.sensor2, a.ch2);
    int bd = g_loopGeometry.findLoopIndex(b.sensor2, b.ch2);
    return au >= 0 && bu >= 0 && ad >= 0 && bd >= 0 &&
           g_loopGeometry.areAdjacent((uint8_t)au, (uint8_t)bu) &&
           g_loopGeometry.areAdjacent((uint8_t)ad, (uint8_t)bd);
}

// ============================================================
// Lane straddle / between-lines detection
// ============================================================
void trafficMonitorOnRawEvent(const EventResult &ev)
{
    if (!g_traffic_rules.enable_lane_violation)
        return;

    uint8_t s = 0, ch = 0;
    if (!parseChannelId(ev.channel_id, s, ch))
        return;

    int cls = vehicleClassIndexFromString(ev.vehicle_class);
    uint64_t now_us = esp_timer_get_time();

    for (int i = 0; i < ADJ_PAIR_COUNT; i++)
    {
        AdjacentPair &p = adjPairs[i];

        if (!p.enabled)
            continue;

        bool currentIsA = (p.s1 == s && p.ch1 == ch);
        bool currentIsB = (p.s2 == s && p.ch2 == ch);

        if (!currentIsA && !currentIsB)
            continue;

        uint8_t os, och;

        if (currentIsA)
        {
            os = p.s2;
            och = p.ch2;
        }
        else
        {
            os = p.s1;
            och = p.ch1;
        }

        RecentEventMini &other = lastEvent[os][och];

        if (!other.valid)
            continue;

        uint32_t start_max = (ev.start_us > other.start_us) ? ev.start_us : other.start_us;
        uint32_t end_min = (ev.end_us < other.end_us) ? ev.end_us : other.end_us;

        if (end_min > start_max)
        {
            uint32_t overlap_ms = (end_min - start_max) / 1000;

            float min_duration = ev.duration_ms;
            if (other.duration_ms < min_duration)
                min_duration = other.duration_ms;

            float overlap_ratio = 0.0f;
            if (min_duration > 10.0f)
                overlap_ratio = (float)overlap_ms / min_duration;

            bool overlap_ok =
                overlap_ms >= g_traffic_rules.min_straddle_overlap_ms &&
                overlap_ratio >= g_traffic_rules.min_straddle_overlap_ratio;

            uint64_t cooldown_us = (uint64_t)g_traffic_rules.lane_violation_cooldown_ms * 1000ULL;

            if (overlap_ok && (now_us - p.last_violation_us) > cooldown_us)
            {
                p.last_violation_us = now_us;
                trafficStatsRecordLaneViolation(cls);

                Serial.printf("[LANE_VIOLATION] %s overlap=%ums ratio=%.2f\n",
                              ev.channel_id,
                              overlap_ms,
                              overlap_ratio);
            }
        }
    }

    lastEvent[s][ch].valid = true;
    lastEvent[s][ch].start_us = ev.start_us;
    lastEvent[s][ch].end_us = ev.end_us;
    lastEvent[s][ch].duration_ms = ev.duration_ms;
    lastEvent[s][ch].cls = cls;
}

// ============================================================
// Single-loop vehicle counting and following distance
// ============================================================
void trafficMonitorOnSingleEvent(const EventResult &ev)
{
    int cls = vehicleClassIndexFromString(ev.vehicle_class);

    float speed_kmh = 0.0f;
    if (ev.spatial_speed_ms > 0.1f)
        speed_kmh = ev.spatial_speed_ms * 3.6f;

    trafficStatsRecordVehicle(cls, speed_kmh);

    if (!g_traffic_rules.enable_distance_violation)
        return;

    uint8_t s = 0, ch = 0;
    if (!parseChannelId(ev.channel_id, s, ch))
        return;

    FollowState &st = channelFollowState[s][ch];

    uint32_t current_start = ev.start_us;
    uint32_t current_end = ev.end_us;

    if (st.valid)
    {
        uint32_t gap_us = current_start - st.ref_end_us;

        if ((gap_us & 0x80000000UL) == 0)
        {
            uint32_t gap_ms = gap_us / 1000;

            if (gap_ms <= g_traffic_rules.max_headway_ms)
            {
                float headway_s = gap_ms / 1000.0f;

                bool violation = false;

                if (headway_s < g_traffic_rules.min_headway_s)
                {
                    violation = true;
                }

                if (speed_kmh > 1.0f)
                {
                    float speed_ms = speed_kmh / 3.6f;
                    float gap_m = speed_ms * headway_s;

                    if (gap_m < g_traffic_rules.min_follow_distance_m)
                        violation = true;
                }
                else if (g_traffic_rules.assume_speed_kmh > 1.0f)
                {
                    float speed_ms = g_traffic_rules.assume_speed_kmh / 3.6f;
                    float gap_m = speed_ms * headway_s;

                    if (gap_m < g_traffic_rules.min_follow_distance_m)
                        violation = true;
                }

                if (violation)
                {
                    trafficStatsRecordDistanceViolation(cls);

                    Serial.printf("[DIST_VIOLATION] channel=%s gap_ms=%u headway=%.2fs speed=%.1fkm/h\n",
                                  ev.channel_id,
                                  gap_ms,
                                  headway_s,
                                  speed_kmh);
                }
            }
        }
    }

    st.valid = true;
    st.ref_end_us = current_end;
    st.speed_ms = speed_kmh / 3.6f;
    st.cls = cls;
}

// ============================================================
// Dual-loop matched vehicle
// ============================================================
void trafficMonitorOnDualMatch(uint8_t pairIdx,
                               const EventResult *e1,
                               const EventResult *e2,
                               float speed_kmh)
{
    if (!e1 || !e2 || pairIdx >= SPEED_PAIR_COUNT)
        return;

    int cls = vehicleClassIndexFromString(e1->vehicle_class);

    trafficStatsRecordVehicle(cls, speed_kmh);

    if (g_traffic_rules.enable_speed_violation)
    {
        float limit = g_traffic_rules.speed_limit_kmh + g_traffic_rules.speed_tolerance_kmh;

        if (speed_kmh > limit)
        {
            trafficStatsRecordSpeedViolation(cls, speed_kmh);

            Serial.printf("[SPEED_VIOLATION] pair=%u speed=%.1f km/h limit=%.1f\n",
                          pairIdx,
                          speed_kmh,
                          limit);
        }
    }

    if (g_traffic_rules.enable_distance_violation)
    {
        const EventResult *first = e1;
        if (e2->start_us < e1->start_us)
            first = e2;

        uint32_t current_ref_start = first->start_us;
        uint32_t current_ref_end = first->end_us;

        FollowState &st = pairFollowState[pairIdx];

        if (st.valid)
        {
            uint32_t gap_us = current_ref_start - st.ref_end_us;

            if ((gap_us & 0x80000000UL) == 0)
            {
                uint32_t gap_ms = gap_us / 1000;

                if (gap_ms <= g_traffic_rules.max_headway_ms)
                {
                    float headway_s = gap_ms / 1000.0f;

                    float current_speed_ms = speed_kmh / 3.6f;
                    float avg_speed_ms = current_speed_ms;

                    if (st.speed_ms > 0.1f)
                        avg_speed_ms = (current_speed_ms + st.speed_ms) * 0.5f;

                    float gap_m = avg_speed_ms * headway_s;

                    bool violation = false;

                    if (headway_s < g_traffic_rules.min_headway_s)
                        violation = true;

                    if (gap_m < g_traffic_rules.min_follow_distance_m)
                        violation = true;

                    if (violation)
                    {
                        trafficStatsRecordDistanceViolation(cls);

                        Serial.printf("[DIST_VIOLATION] pair=%u gap_ms=%u gap_m=%.1f speed=%.1f km/h\n",
                                      pairIdx,
                                      gap_ms,
                                      gap_m,
                                      speed_kmh);
                    }
                }
            }
        }

        st.valid = true;
        st.ref_end_us = current_ref_end;
        st.speed_ms = speed_kmh / 3.6f;
        st.cls = cls;
    }
}

bool trafficMonitorBuildBetweenLinesEvent(uint8_t pairIdx,
                                          const EventResult *e1,
                                          const EventResult *e2,
                                          float speed_kmh,
                                          char *out,
                                          size_t outSize)
{
    if (!e1 || !e2 || !out || outSize == 0 || pairIdx >= SPEED_PAIR_COUNT ||
        !loopCfg[pairIdx].dualLoop)
        return false;

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
    {
        if (betweenLinesMatches[i].valid &&
            now_us - (uint64_t)betweenLinesMatches[i].end_us > SPEED_PAIR_TIMEOUT_US)
            betweenLinesMatches[i].valid = false;
    }

    BetweenLinesMatch current;
    current.valid = true;
    current.pair_idx = pairIdx;
    current.s1 = loopCfg[pairIdx].sensor1;
    current.ch1 = loopCfg[pairIdx].ch1;
    current.s2 = loopCfg[pairIdx].sensor2;
    current.ch2 = loopCfg[pairIdx].ch2;
    current.start_us = e1->start_us < e2->start_us ? e1->start_us : e2->start_us;
    current.end_us = e1->end_us > e2->end_us ? e1->end_us : e2->end_us;
    current.duration_ms = e1->duration_ms > e2->duration_ms ? e1->duration_ms : e2->duration_ms;
    current.length_m = (e1->estimated_length_m + e2->estimated_length_m) * 0.5f;
    current.speed_kmh = speed_kmh;
    current.cls = vehicleClassIndexFromString(e1->vehicle_class);

    for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
    {
        BetweenLinesMatch &other = betweenLinesMatches[i];
        if (!other.valid || i == pairIdx || !loopCfg[i].dualLoop ||
            !speedPairsAreAdjacent(loopCfg[pairIdx], loopCfg[i]))
            continue;

        uint32_t overlap_ms = 0;
        float overlap_ratio = 0.0f;
        if (!intervalsOverlap(current, other, overlap_ms, overlap_ratio))
            continue;

        uint64_t cooldown_us = (uint64_t)g_traffic_rules.lane_violation_cooldown_ms * 1000ULL;
        if (other.end_us > current.end_us &&
            (uint64_t)other.end_us - current.end_us < cooldown_us)
        {
            // Reverse-order delivery is still one match; the first report
            // already owns this pair combination during the cooldown.
            other.valid = false;
            return false;
        }

        char a1[8], a2[8], b1[8], b2[8];
        snprintf(a1, sizeof(a1), "S%uC%u", current.s1 + 1, current.ch1);
        snprintf(a2, sizeof(a2), "S%uC%u", current.s2 + 1, current.ch2);
        snprintf(b1, sizeof(b1), "S%uC%u", other.s1 + 1, other.ch1);
        snprintf(b2, sizeof(b2), "S%uC%u", other.s2 + 1, other.ch2);

        float aggregate_speed = (current.speed_kmh + other.speed_kmh) * 0.5f;
        float aggregate_length = (current.length_m + other.length_m) * 0.5f;
        float aggregate_duration = (current.duration_ms + other.duration_ms) * 0.5f;
        snprintf(out, outSize,
                 "SPEED|between_lines:1|pair_a:%u|pair_b:%u|speed:%.1f|len:%.2f|dur:%.1f|overlap:%u|ratio:%.2f|a:%s,%s|b:%s,%s",
                 pairIdx, other.pair_idx, aggregate_speed, aggregate_length,
                 aggregate_duration, overlap_ms, overlap_ratio, a1, a2, b1, b2);

        if (g_traffic_rules.enable_lane_violation)
            trafficStatsRecordLaneViolation(current.cls);

        other.valid = false;
        return true;
    }

    // Replace an older match from the same pair. This also makes repeated
    // detections from one pair unable to combine with stale data.
    betweenLinesMatches[pairIdx] = current;
    return false;
}

// ============================================================
// Configure adjacent loops
// ============================================================
void trafficMonitorSetAdjacent(uint8_t s1, uint8_t ch1,
                               uint8_t s2, uint8_t ch2,
                               bool enabled)
{
    if (s1 >= 2 || ch1 >= 4 || s2 >= 2 || ch2 >= 4)
        return;

    for (int i = 0; i < ADJ_PAIR_COUNT; i++)
    {
        AdjacentPair &p = adjPairs[i];

        bool match =
            (p.s1 == s1 && p.ch1 == ch1 && p.s2 == s2 && p.ch2 == ch2) ||
            (p.s1 == s2 && p.ch1 == ch2 && p.s2 == s1 && p.ch2 == ch1);

        if (match)
        {
            p.enabled = enabled;
            return;
        }
    }

    for (int i = 0; i < ADJ_PAIR_COUNT; i++)
    {
        if (!adjPairs[i].enabled)
        {
            adjPairs[i].enabled = enabled;
            adjPairs[i].s1 = s1;
            adjPairs[i].ch1 = ch1;
            adjPairs[i].s2 = s2;
            adjPairs[i].ch2 = ch2;
            adjPairs[i].last_violation_us = 0;
            return;
        }
    }
}

// ============================================================
// Sync adjacent pairs from LoopGeometry
// ============================================================
void trafficMonitorSyncFromGeometry()
{
    // Disable all existing pairs
    for (int i = 0; i < ADJ_PAIR_COUNT; i++)
    {
        adjPairs[i].enabled = false;
    }

    // Rebuild from geometry
    uint8_t count = 0;
    for (int i = 0; i < g_loopGeometry.getLoopCount() && count < ADJ_PAIR_COUNT; i++)
    {
        const LoopGeometry &loop = g_loopGeometry.getLoop(i);
        for (int a = 0; a < loop.adjacent_count && count < ADJ_PAIR_COUNT; a++)
        {
            int adjIdx = loop.adjacent_loops[a];
            if (adjIdx < 0 || adjIdx >= g_loopGeometry.getLoopCount())
                continue;
            if (adjIdx <= i) continue; // avoid duplicates

            const LoopGeometry &adj = g_loopGeometry.getLoop(adjIdx);

            // Check if pair already exists
            bool exists = false;
            for (int j = 0; j < ADJ_PAIR_COUNT; j++)
            {
                if (adjPairs[j].s1 == loop.sensor_id && adjPairs[j].ch1 == loop.channel_id &&
                    adjPairs[j].s2 == adj.sensor_id && adjPairs[j].ch2 == adj.channel_id)
                {
                    adjPairs[j].enabled = true;
                    exists = true;
                    break;
                }
                if (adjPairs[j].s1 == adj.sensor_id && adjPairs[j].ch1 == adj.channel_id &&
                    adjPairs[j].s2 == loop.sensor_id && adjPairs[j].ch2 == loop.channel_id)
                {
                    adjPairs[j].enabled = true;
                    exists = true;
                    break;
                }
            }

            if (!exists && count < ADJ_PAIR_COUNT)
            {
                adjPairs[count] = AdjacentPair(true,
                    loop.sensor_id, loop.channel_id,
                    adj.sensor_id, adj.channel_id);
                count++;
            }
        }
    }

    Serial.printf("[TrafficMonitor] Synced %d adjacent pairs from geometry\n", count);
}
