#include "LoopGeometry.h"
#include <cstring>
#include <math.h>

LoopGeometryManager g_loopGeometry;

// ============================================================
// Constructor
// ============================================================
LoopGeometryManager::LoopGeometryManager()
    : loop_count_(0)
{
    reset();
}

// ============================================================
// Reset
// ============================================================
void LoopGeometryManager::reset()
{
    memset(&site_cfg_, 0, sizeof(site_cfg_));
    loop_count_ = 0;
    for (int i = 0; i < MAX_LOOPS; i++)
    {
        loops_[i] = LoopGeometry();
    }
}

// ============================================================
// Load Defaults (4-lane road, 2 sensors x 4 channels)
// ============================================================
void LoopGeometryManager::loadDefaults()
{
    reset();

    snprintf(site_cfg_.site_id, sizeof(site_cfg_.site_id), "SITE-01");
    site_cfg_.lane_count = 4;
    site_cfg_.lane_width_m = 3.5f;
    site_cfg_.speed_limit_kmh = 100.0f;
    site_cfg_.road_direction_deg = 0.0f;
    site_cfg_.left_to_right_direction = true;

    // Sensor 1 (upstream): lanes 0-3
    loops_[0] = LoopGeometry(0, 0, 0, 0.0f, 0.0f, "S1C0");
    loops_[1] = LoopGeometry(0, 1, 1, 3.5f, 0.0f, "S1C1");
    loops_[2] = LoopGeometry(0, 2, 2, 7.0f, 0.0f, "S1C2");
    loops_[3] = LoopGeometry(0, 3, 3, 10.5f, 0.0f, "S1C3");

    // Sensor 2 (downstream): lanes 0-3
    loops_[4] = LoopGeometry(1, 0, 0, 0.0f, 10.0f, "S2C0");
    loops_[5] = LoopGeometry(1, 1, 1, 3.5f, 10.0f, "S2C1");
    loops_[6] = LoopGeometry(1, 2, 2, 7.0f, 10.0f, "S2C2");
    loops_[7] = LoopGeometry(1, 3, 3, 10.5f, 10.0f, "S2C3");

    loop_count_ = 8;

    computeAdjacency();
}

// ============================================================
// Site Config
// ============================================================
void LoopGeometryManager::setSiteConfig(const SiteGeometryConfig &cfg) { site_cfg_ = cfg; }
SiteGeometryConfig LoopGeometryManager::getSiteConfig() const { return site_cfg_; }

// ============================================================
// Loop Access
// ============================================================
void LoopGeometryManager::setLoop(uint8_t index, const LoopGeometry &loop)
{
    if (index >= MAX_LOOPS) return;
    loops_[index] = loop;
    if (index >= loop_count_)
        loop_count_ = index + 1;
}

LoopGeometry LoopGeometryManager::getLoop(uint8_t index) const
{
    if (index >= MAX_LOOPS) return LoopGeometry();
    return loops_[index];
}

uint8_t LoopGeometryManager::getLoopCount() const { return loop_count_; }

// ============================================================
// Find Loop
// ============================================================
const LoopGeometry *LoopGeometryManager::findLoop(uint8_t sensor, uint8_t channel) const
{
    for (int i = 0; i < loop_count_; i++)
    {
        if (loops_[i].sensor_id == sensor && loops_[i].channel_id == channel)
            return &loops_[i];
    }
    return nullptr;
}

int LoopGeometryManager::findLoopIndex(uint8_t sensor, uint8_t channel) const
{
    for (int i = 0; i < loop_count_; i++)
    {
        if (loops_[i].sensor_id == sensor && loops_[i].channel_id == channel)
            return i;
    }
    return -1;
}

// ============================================================
// Adjacency Query
// ============================================================
bool LoopGeometryManager::areAdjacent(uint8_t idx1, uint8_t idx2) const
{
    if (idx1 >= loop_count_ || idx2 >= loop_count_) return false;
    if (idx1 == idx2) return false;

    const LoopGeometry &a = loops_[idx1];
    for (int i = 0; i < a.adjacent_count; i++)
    {
        if (a.adjacent_loops[i] == (int)idx2)
            return true;
    }
    return false;
}

bool LoopGeometryManager::getAdjacentLoops(uint8_t sensor, uint8_t channel,
                                            uint8_t adjSensors[], uint8_t adjChannels[],
                                            uint8_t &count) const
{
    count = 0;
    int idx = findLoopIndex(sensor, channel);
    if (idx < 0) return false;

    const LoopGeometry &loop = loops_[idx];
    for (int i = 0; i < loop.adjacent_count && count < MAX_ADJACENT_PER_LOOP; i++)
    {
        int adjIdx = loop.adjacent_loops[i];
        if (adjIdx >= 0 && adjIdx < loop_count_)
        {
            adjSensors[count] = loops_[adjIdx].sensor_id;
            adjChannels[count] = loops_[adjIdx].channel_id;
            count++;
        }
    }
    return count > 0;
}

// ============================================================
// Compute Adjacency from Geometry
// ============================================================
void LoopGeometryManager::computeAdjacency()
{
    // Clear existing adjacency
    for (int i = 0; i < loop_count_; i++)
    {
        loops_[i].adjacent_count = 0;
        for (int j = 0; j < MAX_ADJACENT_PER_LOOP; j++)
            loops_[i].adjacent_loops[j] = -1;
    }

    float lane_w = site_cfg_.lane_width_m;
    if (lane_w <= 0.0f) lane_w = 3.5f;

    // Adjacency threshold: loops are adjacent if:
    // - Same sensor, different channel, same y (lateral neighbors)
    // - Same channel, different sensor, similar x (longitudinal speed pairs)
    // - Cross-loop: small x diff AND small y diff

    float lateral_max = lane_w * 1.2f;
    float longitudinal_max = 20.0f;

    for (int i = 0; i < loop_count_; i++)
    {
        for (int j = i + 1; j < loop_count_; j++)
        {
            const LoopGeometry &a = loops_[i];
            const LoopGeometry &b = loops_[j];

            float dx = fabsf(a.x_m - b.x_m);
            float dy = fabsf(a.y_m - b.y_m);

            bool adjacent = false;

            // Same sensor, same y row -> lateral neighbors (across lanes)
            if (a.sensor_id == b.sensor_id && dy < 0.1f && dx < lateral_max && dx > 0.1f)
            {
                adjacent = true;
            }
            // Same channel, different sensor -> longitudinal (speed pair)
            else if (a.channel_id == b.channel_id && a.sensor_id != b.sensor_id && dx < 0.1f && dy < longitudinal_max)
            {
                adjacent = true;
            }
            // Cross-loop: close in both dimensions
            else if (dx < lateral_max && dy < 2.0f && (dx + dy) < (lane_w + 2.0f))
            {
                adjacent = true;
            }

            if (adjacent && loops_[i].adjacent_count < MAX_ADJACENT_PER_LOOP && loops_[j].adjacent_count < MAX_ADJACENT_PER_LOOP)
            {
                loops_[i].adjacent_loops[loops_[i].adjacent_count++] = j;
                loops_[j].adjacent_loops[loops_[j].adjacent_count++] = i;
            }
        }
    }
}

// ============================================================
// Distance Between Loops
// ============================================================
float LoopGeometryManager::distanceBetween(const LoopGeometry &a, const LoopGeometry &b)
{
    float dx = a.x_m - b.x_m;
    float dy = a.y_m - b.y_m;
    return sqrtf(dx * dx + dy * dy);
}

float LoopGeometryManager::getLoopDistance(uint8_t idx1, uint8_t idx2) const
{
    if (idx1 >= loop_count_ || idx2 >= loop_count_) return 0.0f;
    return distanceBetween(loops_[idx1], loops_[idx2]);
}

uint8_t LoopGeometryManager::getLaneId(uint8_t sensor, uint8_t channel) const
{
    const LoopGeometry *loop = findLoop(sensor, channel);
    return loop ? loop->lane_id : 0;
}

// ============================================================
// Build Status String
// ============================================================
void LoopGeometryManager::buildStatusString(char *buf, size_t bufSize) const
{
    int pos = 0;
    pos += snprintf(buf + pos, bufSize - pos,
                    "site:%s|lanes:%u|lane_w:%.1f|speed_limit:%.0f|dir:%.0f|loops:%u",
                    site_cfg_.site_id,
                    site_cfg_.lane_count,
                    site_cfg_.lane_width_m,
                    site_cfg_.speed_limit_kmh,
                    site_cfg_.road_direction_deg,
                    loop_count_);

    for (int i = 0; i < loop_count_ && pos < (int)bufSize - 80; i++)
    {
        const LoopGeometry &l = loops_[i];
        pos += snprintf(buf + pos, bufSize - pos,
                        "|%s:s%u/c%u/l%u/x%.1f/y%.1f/adj%d",
                        l.name, l.sensor_id, l.channel_id, l.lane_id,
                        l.x_m, l.y_m, l.adjacent_count);
    }
}

// ============================================================
// Parse from Command
// Format: SET_LOOP_GEOMETRY|site_id|lane_count|lane_width|speed_limit|dir
//   then: SET_LOOP_ADD|sensor|channel|lane|x_m|y_m|name
// ============================================================
bool LoopGeometryManager::parseFromCommand(const String &cmd)
{
    if (cmd.startsWith("SET_LOOP_GEOMETRY|"))
    {
        String data = cmd.substring(18);
        float p[4] = {};
        int start = 0;
        int cnt = 0;
        String siteId = "";

        // Parse site_id (first token, may contain non-numeric)
        int pipeIdx = data.indexOf('|', start);
        if (pipeIdx < 0) return false;
        siteId = data.substring(start, pipeIdx);
        start = pipeIdx + 1;

        // Parse numeric params
        while (cnt < 4)
        {
            pipeIdx = data.indexOf('|', start);
            String tok = (pipeIdx < 0) ? data.substring(start) : data.substring(start, pipeIdx);
            tok.trim();
            if (tok.length())
                p[cnt++] = tok.toFloat();
            if (pipeIdx < 0) break;
            start = pipeIdx + 1;
        }

        if (cnt < 4) return false;

        reset();
        snprintf(site_cfg_.site_id, sizeof(site_cfg_.site_id), "%s", siteId.c_str());
        site_cfg_.lane_count = (uint8_t)p[0];
        site_cfg_.lane_width_m = p[1];
        site_cfg_.speed_limit_kmh = p[2];
        site_cfg_.road_direction_deg = p[3];

        return true;
    }

    if (cmd.startsWith("SET_LOOP_ADD|"))
    {
        if (loop_count_ >= MAX_LOOPS) return false;

        float p[5] = {};
        int start = 13;
        int cnt = 0;

        while (cnt < 5)
        {
            int pipeIdx = cmd.indexOf('|', start);
            String tok = (pipeIdx < 0) ? cmd.substring(start) : cmd.substring(start, pipeIdx);
            tok.trim();
            if (tok.length())
                p[cnt++] = tok.toFloat();
            if (pipeIdx < 0) break;
            start = pipeIdx + 1;
        }

        if (cnt < 5) return false;

        uint8_t sensor = (uint8_t)p[0];
        uint8_t channel = (uint8_t)p[1];
        uint8_t lane = (uint8_t)p[2];
        float x = p[3];
        float y = p[4];

        if (sensor >= 2 || channel >= 4) return false;

        // Check if already exists
        int existing = findLoopIndex(sensor, channel);
        char name[16];
        snprintf(name, sizeof(name), "S%dC%d", sensor + 1, channel);

        LoopGeometry loop(sensor, channel, lane, x, y, name);

        if (existing >= 0)
        {
            loops_[existing] = loop;
        }
        else
        {
            loops_[loop_count_++] = loop;
        }

        return true;
    }

    if (cmd.startsWith("SET_LOOP_DONE"))
    {
        computeAdjacency();
        return true;
    }

    return false;
}
