// ============================================================
// WsCommandHandler.cpp  —  WebSocket command parsing & handling
// ============================================================
#include "WsCommandHandler.h"
#include "WsUtils.h"
#include "TrafficStats.h"
#include "TrafficMonitor.h"
#include "LoopGeometry.h"
#include "PersistentConfig.h"
#include "MqttHandler.h"
#include "LteModem.h"

namespace
{
  enum WsEventType : uint8_t
  {
    WS_TYPE_ERROR = 0,
    WS_TYPE_DISCONNECTED,
    WS_TYPE_CONNECTED,
    WS_TYPE_TEXT = 3,
    WS_TYPE_BIN,
    WS_TYPE_PING,
    WS_TYPE_PONG,
    WS_TYPE_FRAGMENT_TEXT_START,
    WS_TYPE_FRAGMENT_BIN_START,
    WS_TYPE_FRAGMENT,
    WS_TYPE_FRAGMENT_FIN,
    WS_TYPE_CLOSE,
    WS_TYPE_HEARTBEAT,
    WS_TYPE_WARNING,
    WS_TYPE_EVENT
  };
}

// ============================================================
// Internal: parse pipe-delimited floats from a String
// ============================================================
static int parsePipeFloats(const String &s, float *out, int max)
{
  int cnt = 0, start = 0;
  while (cnt < max)
  {
    int p = s.indexOf('|', start);
    String tok = (p < 0) ? s.substring(start) : s.substring(start, p);
    tok.trim();
    if (tok.length())
      out[cnt++] = tok.toFloat();
    if (p < 0)
      break;
    start = p + 1;
  }
  return cnt;
}

void processSystemCommand(const String &cmd, void (*replyFunc)(uint8_t num, const char *msg), uint32_t num)
{
  if (cmd.startsWith("CALIBRATE_CHANNEL|"))
  {
    float p[2] = {};
    if (parsePipeFloats(cmd.substring(18), p, 2) == 2)
    {
      int s = (int)p[0];
      int ch = (int)p[1];
      if (s < 0 || s >= 2 || ch < 0 || ch >= 4)
      {
        replyFunc(num, "ERROR|Invalid_channel");
        return;
      }

      det[s][ch].startCalibration();
      char msg[48];
      snprintf(msg, sizeof(msg), "CALIBRATION_STARTED|%s", det[s][ch].id());
      replyFunc(num, msg);
      // wsSendToClient(num, msg);
      Serial.printf("[CAL] Channel calibration started: %s\n", det[s][ch].id());
    }
  }
  else if (cmd.startsWith("CALIBRATE"))
  {
    for (int s = 0; s < 2; s++)
      for (int ch = 0; ch < 4; ch++)
        det[s][ch].startCalibration();
    replyFunc(num, "CALIBRATION_STARTED");
  }
  else if (cmd.startsWith("CONFIG|"))
  {
    float p[6] = {};
    int count = parsePipeFloats(cmd.substring(7), p, 6);
    if (count >= 2)
    {
      bool enabled = (p[0] != 0);
      float distance = p[1];
      if (distance <= 0.0f)
      {
        replyFunc(num, "ERROR|Invalid_distance");
        return;
      }
      uint8_t sensor1 = 0;
      uint8_t ch1 = 0;
      uint8_t sensor2 = 1;
      uint8_t ch2 = 0;

      if (count >= 6)
      {
        sensor1 = (uint8_t)constrain((int)p[2], 0, 1);
        ch1 = (uint8_t)constrain((int)p[3], 0, 3);
        sensor2 = (uint8_t)constrain((int)p[4], 0, 1);
        ch2 = (uint8_t)constrain((int)p[5], 0, 3);
      }
      else
      {
        ch1 = (uint8_t)constrain((int)p[2], 0, 3);
        ch2 = (uint8_t)constrain((int)p[3], 0, 3);
      }

      char id1[8], id2[8];
      applySpeedPairConfig(0, enabled, distance, sensor1, ch1, sensor2, ch2);
      formatLoopChannelId(loopCfg[0].sensor1, loopCfg[0].ch1, id1, sizeof(id1));
      formatLoopChannelId(loopCfg[0].sensor2, loopCfg[0].ch2, id2, sizeof(id2));
      Serial.printf("[CONFIG] Pair=0 Dual=%d Dist=%.2fm Endpoints=%s<->%s\n",
                    loopCfg[0].dualLoop, loopCfg[0].distance, id1, id2);
      sendSpeedPairConfig(num, 0);
      PersistentConfig::saveLoopConfig(0, loopCfg[0]);
    }
  }
  else if (cmd.startsWith("CONFIG_SET|"))
  {
    float p[7] = {};
    int count = parsePipeFloats(cmd.substring(11), p, 7);
    if (count == 7)
    {
      uint8_t idx = (uint8_t)constrain((int)p[0], 0, SPEED_PAIR_COUNT - 1);
      if (p[2] <= 0.0f)
      {
        replyFunc(num, "ERROR|Invalid_distance");
        return;
      }
      applySpeedPairConfig(
          idx,
          ((int)p[1] != 0),
          p[2],
          (uint8_t)p[3],
          (uint8_t)p[4],
          (uint8_t)p[5],
          (uint8_t)p[6]);

      char id1[8], id2[8];
      formatLoopChannelId(loopCfg[idx].sensor1, loopCfg[idx].ch1, id1, sizeof(id1));
      formatLoopChannelId(loopCfg[idx].sensor2, loopCfg[idx].ch2, id2, sizeof(id2));
      Serial.printf("[CONFIG] Pair=%u Dual=%d Dist=%.2fm Endpoints=%s<->%s\n",
                    idx, loopCfg[idx].dualLoop, loopCfg[idx].distance, id1, id2);
      sendSpeedPairConfig(num, idx);
      PersistentConfig::saveLoopConfig(idx, loopCfg[idx]);
    }
  }
  else if (cmd.startsWith("SET_LC|"))
  {
    float p[4] = {};
    if (parsePipeFloats(cmd.substring(7), p, 4) == 4)
    {
      int s = (int)p[0], ch = (int)p[1];
      if (s >= 0 && s < 2 && ch >= 0 && ch < 4)
      {
        auto &lc = (s == 0) ? sensor1LC : sensor2LC;
        auto &bus = (s == 0) ? I2C_Bus0 : I2C_Bus1;
        auto &ldc = (s == 0) ? ldc1 : ldc2;
        auto &mtx = (s == 0) ? i2c0Mutex : i2c1Mutex;
        lc.L[ch] = p[2];
        lc.C[ch] = p[3];
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(10)) == pdTRUE)
        {
          ldc.LDC1614_reset_sensor(bus);
          ldc.LDC1614_single_channel_config(bus, ch, lc.L[ch], lc.C[ch]);
          ldc.LDC1614_set_conversion_time(bus, ch, lc.conversion_time[ch]);
          ldc.LDC1614_set_driver_current(bus, ch, lc.driver_current[ch]);
          xSemaphoreGive(mtx);
          PersistentConfig::saveSensorLC(s, lc);
          replyFunc(num, "LC_ACK");
        }
      }
    }
  }
  else if (cmd.startsWith("SET_THRESHOLD|"))
  {
    int first = cmd.indexOf('|', 14);
    if (first == -1)
      return;
    String param = cmd.substring(14, first);
    float val = cmd.substring(first + 1).toFloat();

    for (int s = 0; s < 2; s++)
    {
      for (int ch = 0; ch < 4; ch++)
      {
        DetectorConfig cfg = det[s][ch].getConfig();
        if (param == "enter")
          cfg.enter_thresh = val;
        else if (param == "exit_ratio")
          cfg.exit_ratio = val;
        else if (param == "hysteresis")
          cfg.exit_hysteresis_cnt = (uint32_t)val;
        det[s][ch].setConfig(cfg);
      }
    }
    replyFunc(num, "THRESHOLD_ACK");
    PersistentConfig::saveDetectorConfigs();
  }
  else if (cmd.startsWith("SET_EVENT_RANGE|"))
  {
    float p[2];
    if (parsePipeFloats(cmd.substring(16), p, 2) == 2)
    {
      uint32_t min_ms = (uint32_t)p[0];
      uint32_t max_ms = (uint32_t)p[1];
      if (min_ms == 0 || max_ms <= min_ms)
      {
        replyFunc(num, "ERROR|Invalid_event_range");
        return;
      }
      for (int s = 0; s < 2; s++)
        for (int ch = 0; ch < 4; ch++)
        {
          DetectorConfig cfg = det[s][ch].getConfig();
          cfg.min_event_ms = min_ms;
          cfg.max_event_ms = max_ms;
          det[s][ch].setConfig(cfg);
        }
      replyFunc(num, "EVENT_RANGE_ACK");
      PersistentConfig::saveDetectorConfigs();
    }
  }
  else if (cmd.startsWith("SET_CLASSIFY|"))
  {
    float p[27] = {};
    int count = parsePipeFloats(cmd.substring(13), p, 27);
    if (count >= 9)
    {
      for (int s = 0; s < 2; s++)
        for (int ch = 0; ch < 4; ch++)
        {
          DetectorConfig cfg = det[s][ch].getConfig();
          cfg.motor_max_len = p[0];
          cfg.car_max_len = p[1];
          cfg.pickup_max_len = p[2];
          cfg.van_max_len = p[3];
          cfg.bus_max_len = p[4];
          cfg.truck_s_max_len = p[5];
          cfg.truck_2_max_len = p[6];
          cfg.truck_3_max_len = p[7];
          cfg.truck_4_plus_min_len = p[8];

          if (count >= 11)
          {
            cfg.peak_prominence_ratio = p[9];
            cfg.min_axle_distance_ms = p[10];
          }
          if (count >= 27)
          {
            cfg.classify_rise_short_ms = p[11];
            cfg.classify_rise_mid_ms = p[12];
            cfg.classify_rise_long_ms = p[13];
            cfg.classify_energy_low = p[14];
            cfg.classify_energy_mid = p[15];
            cfg.classify_energy_high = p[16];
            cfg.classify_crest_spiky = p[17];
            cfg.classify_crest_broad = p[18];
            cfg.classify_skew_tol = p[19];
            cfg.classify_skew_high = p[20];
            cfg.classify_com_center_min = p[21];
            cfg.classify_com_center_max = p[22];
            cfg.classify_width_medium = p[23];
            cfg.classify_width_wide = p[24];
            cfg.classify_std_peak_high = p[25];
            cfg.classify_std_peak_low = p[26];
          }
          det[s][ch].setConfig(cfg);
        }
      replyFunc(num, "CLASSIFY_ACK");
      PersistentConfig::saveDetectorConfigs();
    }
  }
  else if (cmd.startsWith("SET_DETECTOR|"))
  {
    float p[5] = {};
    if (parsePipeFloats(cmd.substring(13), p, 5) == 5)
    {
      if (p[0] <= 0.0f || p[1] <= 0.0f || p[2] <= 0.0f || p[3] <= 0.0f || p[4] <= 0.0f)
      {
        replyFunc(num, "ERROR|Invalid_detector_values");
        return;
      }
      for (int s = 0; s < 2; s++)
      {
        for (int ch = 0; ch < 4; ch++)
        {
          DetectorConfig cfg = det[s][ch].getConfig();
          cfg.confirm_samples = (uint32_t)p[0];
          cfg.min_event_samples = (uint32_t)p[1];
          cfg.peak_to_baseline_ratio = p[2];
          cfg.enter_hysteresis_ratio = p[3];
          cfg.exit_hysteresis_ratio = p[4];
          det[s][ch].setConfig(cfg);
        }
      }
      replyFunc(num, "DETECTOR_ACK");
      PersistentConfig::saveDetectorConfigs();
    }
  }
  else if (cmd.startsWith("SET_AUTO_THRESH|"))
  {
    float p[3] = {};

    if (parsePipeFloats(cmd.substring(16), p, 3) == 3)
    {
      if (p[1] <= 0.0f || p[2] <= 0.0f)
      {
        replyFunc(num, "ERROR|Invalid_sigma_values");
        return;
      }
      for (int s = 0; s < 2; s++)
      {
        for (int ch = 0; ch < 4; ch++)
        {
          DetectorConfig cfg = det[s][ch].getConfig();

          cfg.auto_threshold = ((int)p[0] != 0);
          cfg.enter_sigma = p[1];
          cfg.abs_sigma = p[2];

          det[s][ch].setConfig(cfg);
          det[s][ch].recalcThresholds();
        }
      }

      replyFunc(num, "AUTO_THRESH_ACK");
      PersistentConfig::saveDetectorConfigs();
    }
  }
  else if (cmd.startsWith("GET_SPEED_CONFIG"))
  {
    sendAllSpeedPairConfigs(num);
  }
  else if (cmd == "GET_SPEED_STATE")
  {
    sendAllSpeedResults(num);
  }
  else if (cmd.startsWith("GET_CONFIG"))
  {
    DetectorConfig cfg = det[0][0].getConfig();
    char msg[640];
    snprintf(msg, sizeof(msg),
             "CONFIG|enter:%.6f|abs:%.6f|exit_ratio:%.2f|hyst:%u|min_ms:%u|max_ms:%u|motor:%.1f|car:%.1f|pickup:%.1f|van:%.1f|bus:%.1f|truckS:%.1f|truck2:%.1f|truck3:%.1f|truck4min:%.1f|prom:%.2f|axle_ms:%.1f|confirm:%u|min_samples:%u|peak_ratio:%.2f|enter_hyst:%.2f|exit_hyst:%.2f|auto:%d|enter_sigma:%.2f|abs_sigma:%.2f|dual:%d|dist:%.1f|s1:%u|c1:%u|s2:%u|c2:%u|default_kmh:%.1f|rise_short:%.1f|rise_mid:%.1f|rise_long:%.1f|energy_low:%.6f|energy_mid:%.6f|energy_high:%.6f|crest_spiky:%.2f|crest_broad:%.2f|skew_tol:%.2f|skew_high:%.2f|com_min:%.2f|com_max:%.2f|width_mid:%.2f|width_wide:%.2f|std_high:%.2f|std_low:%.2f",
             cfg.enter_thresh, cfg.absolute_min_dev, cfg.exit_ratio, cfg.exit_hysteresis_cnt,
             cfg.min_event_ms, cfg.max_event_ms,
             cfg.motor_max_len, cfg.car_max_len, cfg.pickup_max_len, cfg.van_max_len, cfg.bus_max_len,
             cfg.truck_s_max_len, cfg.truck_2_max_len, cfg.truck_3_max_len, cfg.truck_4_plus_min_len,
             cfg.peak_prominence_ratio, cfg.min_axle_distance_ms,
             cfg.confirm_samples, cfg.min_event_samples,
             cfg.peak_to_baseline_ratio, cfg.enter_hysteresis_ratio, cfg.exit_hysteresis_ratio,
             cfg.auto_threshold ? 1 : 0,
             cfg.enter_sigma, cfg.abs_sigma,
             loopCfg[0].dualLoop ? 1 : 0, loopCfg[0].distance,
             loopCfg[0].sensor1, loopCfg[0].ch1, loopCfg[0].sensor2, loopCfg[0].ch2,
             cfg.default_speed_kmh,
             cfg.classify_rise_short_ms, cfg.classify_rise_mid_ms, cfg.classify_rise_long_ms,
             cfg.classify_energy_low, cfg.classify_energy_mid, cfg.classify_energy_high,
             cfg.classify_crest_spiky, cfg.classify_crest_broad,
             cfg.classify_skew_tol, cfg.classify_skew_high,
             cfg.classify_com_center_min, cfg.classify_com_center_max,
             cfg.classify_width_medium, cfg.classify_width_wide,
             cfg.classify_std_peak_high, cfg.classify_std_peak_low);
    replyFunc(num, msg);
    sendAllSpeedPairConfigs(num);
  }
  else if (cmd.startsWith("GET_CPU"))
  {
    char msg[32];
    snprintf(msg, sizeof(msg), "CPU_ACK|%u|%u", cpu_usage_core0, cpu_usage_core1);
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("GET_STATUS"))
  {
    char msg[128] = "STATUS";
    for (int s = 0; s < 2; s++)
      for (int ch = 0; ch < 4; ch++)
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "|%s:%.0f", det[s][ch].id(), det[s][ch].baseline());
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("GET_NOISE"))
  {
    char msg[256];
    int pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "NOISE");

    for (int s = 0; s < 2; s++)
    {
      for (int ch = 0; ch < 4; ch++)
      {
        pos += snprintf(
            msg + pos,
            sizeof(msg) - pos,
            "|%s:%.2f/%.2f/%.6f",
            det[s][ch].id(),
            det[s][ch].noiseStd(),
            det[s][ch].noiseRms(),
            det[s][ch].noisePercent());

        if (pos >= (int)sizeof(msg))
        {
          break;
        }
      }
    }
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("GET_CALIB_STATUS"))
  {
    char msg[1024];
    int pos = 0;
    pos += snprintf(msg + pos, sizeof(msg) - pos, "CALIB_STATUS");
    for (int s = 0; s < 2; s++)
    {
      for (int ch = 0; ch < 4; ch++)
      {
        pos += snprintf(msg + pos, sizeof(msg) - pos, "|%s:", det[s][ch].id());
        det[s][ch].buildCalibStatus(msg + pos, sizeof(msg) - pos);
        pos = strlen(msg);
        if (pos >= (int)sizeof(msg) - 128)
          break;
      }
      if (pos >= (int)sizeof(msg) - 128)
        break;
    }
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("GET_LOOP_GEOMETRY"))
  {
    char msg[1024];
    g_loopGeometry.buildStatusString(msg, sizeof(msg));
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("SET_LOOP_GEOMETRY|") || cmd.startsWith("SET_LOOP_ADD|") || cmd.startsWith("SET_LOOP_DONE"))
  {
    if (g_loopGeometry.parseFromCommand(cmd))
    {
      if (cmd.startsWith("SET_LOOP_DONE"))
      {
        trafficMonitorSyncFromGeometry();
        replyFunc(num, "LOOP_GEOMETRY_ACK|adjacency_computed");
      }
      else
      {
        replyFunc(num, "LOOP_GEOMETRY_ACK");
      }
      PersistentConfig::saveLoopGeometry();
    }
    else
    {
      replyFunc(num, "ERROR|Invalid_loop_geometry");
    }
  }
  else if (cmd.startsWith("SET_DEFAULT_KMH"))
  {
    float p[1] = {};

    if (parsePipeFloats(cmd.substring(16), p, 1) == 1)
    {
      for (int s = 0; s < 2; s++)
      {
        for (int ch = 0; ch < 4; ch++)
        {
          DetectorConfig cfg = det[s][ch].getConfig();

          cfg.default_speed_kmh = p[0];

          det[s][ch].setConfig(cfg);
        }
      }

      replyFunc(num, "DEFAULT_KMH_ACK");
      PersistentConfig::saveDetectorConfigs();
    }
  }
  else if (cmd.startsWith("RESET"))
  {
    ESP.restart();
  }
  else if (cmd.startsWith("GET_REPORT"))
  {
    bool clear_after = false;
    int p_index = cmd.indexOf('|');
    if (p_index >= 0)
    {
      int mode = cmd.substring(p_index + 1).toInt();
      clear_after = (mode == 1);
    }
    String report = trafficStatsBuildReport(clear_after);
    replyFunc(num, report.c_str());
  }
  else if (cmd.startsWith("RESET_STATS"))
  {
    trafficStatsReset();
    replyFunc(num, "STATS_RESET_ACK");
  }
  else if (cmd.startsWith("SET_REPORT_INTERVAL|"))
  {
    float p[1] = {};
    int p_index = cmd.indexOf('|');
    if (p_index >= 0 && parsePipeFloats(cmd.substring(p_index + 1), p, 1) == 1)
    {
      uint32_t minutes = (uint32_t)p[0];
      if (minutes == 0)
      {
        g_report_cfg.enabled = false;
        g_report_cfg.interval_ms = 0;
      }
      else
      {
        g_report_cfg.enabled = true;
        g_report_cfg.interval_ms = minutes * 60000UL;
      }
      char msg[64];
      snprintf(msg, sizeof(msg), "REPORT_INTERVAL_ACK|%u", minutes);
      replyFunc(num, msg);
    }
    else
    {
      replyFunc(num, "ERROR|Invalid_report_interval");
    }
  }
  else if (cmd.startsWith("SET_REPORT_ENABLE|"))
  {
    float p[1] = {};
    int p_index = cmd.indexOf('|');
    if (p_index >= 0 && parsePipeFloats(cmd.substring(p_index + 1), p, 1) == 1)
    {
      g_report_cfg.enabled = ((int)p[0] != 0);
      replyFunc(num, g_report_cfg.enabled ? "REPORT_ENABLE_ACK|1" : "REPORT_ENABLE_ACK|0");
      PersistentConfig::saveReportConfig();
    }
    else
    {
      replyFunc(num, "ERROR|Invalid_report_enable");
    }
  }
  else if (cmd.startsWith("SET_REPORT_CLEAR|"))
  {
    float p[1] = {};
    int p_index = cmd.indexOf('|');
    if (p_index >= 0 && parsePipeFloats(cmd.substring(p_index + 1), p, 1) == 1)
    {
      g_report_cfg.periodic_clear = ((int)p[0] != 0);
      replyFunc(num, g_report_cfg.periodic_clear ? "REPORT_CLEAR_ACK|1" : "REPORT_CLEAR_ACK|0");
      PersistentConfig::saveReportConfig();
    }
    else
    {
      replyFunc(num, "ERROR|Invalid_report_clear");
    }
  }
  else if (cmd.startsWith("SET_RULES|"))
  {
    float p[8] = {};
    int p_index = cmd.indexOf('|');
    if (p_index < 0)
    {
      replyFunc(num, "ERROR|Invalid_rules");
      return;
    }
    int count = parsePipeFloats(cmd.substring(p_index + 1), p, 8);
    if (count >= 5)
    {
      g_traffic_rules.speed_limit_kmh = p[0];
      g_traffic_rules.speed_tolerance_kmh = p[1];
      g_traffic_rules.min_follow_distance_m = p[2];
      g_traffic_rules.min_headway_s = p[3];
      g_traffic_rules.max_headway_ms = (uint32_t)p[4];
    }
    if (count >= 7)
    {
      g_traffic_rules.min_straddle_overlap_ms = (uint32_t)p[5];
      g_traffic_rules.min_straddle_overlap_ratio = p[6];
    }
    if (count >= 8)
    {
      g_traffic_rules.assume_speed_kmh = p[7];
    }
    char msg[256];
    snprintf(msg, sizeof(msg),
             "RULES_ACK|limit:%.1f|tol:%.1f|min_dist:%.1f|min_headway:%.2f|max_headway:%lu|straddle_ms:%lu|straddle_ratio:%.2f|assume_kmh:%.1f",
             g_traffic_rules.speed_limit_kmh,
             g_traffic_rules.speed_tolerance_kmh,
             g_traffic_rules.min_follow_distance_m,
             g_traffic_rules.min_headway_s,
             (unsigned long)g_traffic_rules.max_headway_ms,
             (unsigned long)g_traffic_rules.min_straddle_overlap_ms,
             g_traffic_rules.min_straddle_overlap_ratio,
             g_traffic_rules.assume_speed_kmh);
    replyFunc(num, msg);
    PersistentConfig::saveTrafficRules();
  }
  else if (cmd.startsWith("SET_ADJACENT|"))
  {
    float p[5] = {};
    int p_index = cmd.indexOf('|');
    if (p_index >= 0 && parsePipeFloats(cmd.substring(p_index + 1), p, 5) == 5)
    {
      uint8_t s1 = (uint8_t)p[0];
      uint8_t ch1 = (uint8_t)p[1];
      uint8_t s2 = (uint8_t)p[2];
      uint8_t ch2 = (uint8_t)p[3];
      bool enabled = ((int)p[4] != 0);
      trafficMonitorSetAdjacent(s1, ch1, s2, ch2, enabled);
      char msg[64];
      snprintf(msg, sizeof(msg), "ADJACENT_ACK|S%uC%u-S%uC%u|%d",
               s1, ch1, s2, ch2, enabled ? 1 : 0);
      replyFunc(num, msg);
    }
    else
    {
      replyFunc(num, "ERROR|Invalid_adjacent");
    }
  }
  else if (cmd.startsWith("SET_MQTT_ID|"))
  {
    String newId = cmd.substring(12); // After "SET_MQTT_ID|"
    newId.trim();

    if (newId.length() == 0 || newId.length() > 31)
    {
      replyFunc(num, "ERROR|Invalid ID length (1-31 chars)");
      return;
    }

    // Validate: only alphanumeric, hyphen, underscore
    bool valid = true;
    for (int i = 0; i < newId.length(); i++)
    {
      char c = newId[i];
      if (!isalnum(c) && c != '-' && c != '_')
      {
        valid = false;
        break;
      }
    }

    if (!valid)
    {
      replyFunc(num, "ERROR|ID must be alphanumeric, hyphen, underscore only");
      return;
    }

    // Save to NVS
    if (PersistentConfig::setMqttClientId(newId.c_str()))
    {
      // Update runtime variable
      strncpy(mqttClientId, newId.c_str(), sizeof(mqttClientId) - 1);
      mqttClientId[sizeof(mqttClientId) - 1] = '\0';

      char msg[64];
      snprintf(msg, sizeof(msg), "MQTT_ID_ACK|%s|saved|restart_to_apply", mqttClientId);
      replyFunc(num, msg);
      Serial.printf("[CFG] MQTT ID changed to: %s (restart required)\n", mqttClientId);
    }
    else
    {
      replyFunc(num, "ERROR|Failed to save ID");
    }
  }
  else if (cmd == "GET_MQTT_ID")
  {
    char msg[64];
    snprintf(msg, sizeof(msg), "MQTT_ID|%s", mqttClientId);
    replyFunc(num, msg);
  }
  else if (cmd.startsWith("SET_MQTT_SERVER|"))
  {
    String server = cmd.substring(16);
    PersistentConfig::setMqttServer(server.c_str());
    strncpy(mqttServer, server.c_str(), sizeof(mqttServer) - 1);
    replyFunc(num, "MQTT_SERVER_ACK");
  }
  else if (cmd.startsWith("SET_MQTT_FULL|"))
  {
    // SET_MQTT_FULL|id|server|port|ip|user|pass|apn|topic_events|topic_commands|topic_responses
    String rest = cmd.substring(14);
    String parts[10];
    int idx = 0;
    int start = 0;
    for (int i = 0; i < (int)rest.length() && idx < 10; i++) {
      if (rest.charAt(i) == '|') { parts[idx++] = rest.substring(start, i); start = i + 1; }
    }
    if (idx < 10) parts[idx++] = rest.substring(start);
    if (idx >= 7) {
      const char *id_p   = parts[0].c_str();
      const char *srv_p  = parts[1].c_str();
      uint16_t port      = (uint16_t)atoi(parts[2].c_str());
      const char *ip_p   = parts[3].c_str();
      const char *usr_p  = parts[4].c_str();
      const char *pas_p  = parts[5].c_str();
      const char *apn_p  = parts[6].c_str();
      const char *tEvt   = (idx >= 7) ? parts[7].c_str() : "";
      const char *tCmd   = (idx >= 8) ? parts[8].c_str() : "";
      const char *tResp  = (idx >= 9) ? parts[9].c_str() : "";

      PersistentConfig::setMqttClientId(id_p);
      PersistentConfig::setMqttServer(srv_p);

      PersistentConfig::setInt("mqtt_port", (int32_t)port);

      IPAddress ip;
      if (ip.fromString(ip_p)) {
        PersistentConfig::setUint("ip_server", (uint32_t)ip);
        mqttServerIp = ip;
      }

      PersistentConfig::setString("mqtt_user", usr_p);
      PersistentConfig::setString("mqtt_pass", pas_p);
      PersistentConfig::setString("lte_apn",   apn_p);
      PersistentConfig::setString("mqtt_topic_events",            tEvt);
      PersistentConfig::setString("mqtt_topic_commands",          tCmd);
      PersistentConfig::setString("mqtt_topic_command_responses", tResp);

      // update runtime mirrors
      strncpy(mqttClientId, id_p, sizeof(mqttClientId) - 1); mqttClientId[sizeof(mqttClientId) - 1] = '\0';
      strncpy(mqttServer,   srv_p, sizeof(mqttServer)   - 1); mqttServer[sizeof(mqttServer)   - 1] = '\0';
      mqttPort = port;
      strncpy(mqttUser, usr_p, sizeof(mqttUser) - 1); mqttUser[sizeof(mqttUser) - 1] = '\0';
      strncpy(mqttPass, pas_p, sizeof(mqttPass) - 1); mqttPass[sizeof(mqttPass) - 1] = '\0';

      // Reconfigure MQTT client
      mqttClient.setServer(mqttServer, mqttPort);

      replyFunc(num, "MQTT_CFG_FULL_ACK|saved|restart_recommended");
    } else {
      replyFunc(num, "ERROR|Invalid_mqtt_full_payload");
    }
  }
  else if (cmd == "GET_MQTT_CFG")
  {
    char msg[512];
    snprintf(msg, sizeof(msg),
      "MQTT_CFG|%s|%s|%u|%s|%s|%s|%s|%s|%s|%s",
      mqttClientId, mqttServer, (unsigned)mqttPort,
      mqttServerIp.toString().c_str(),
      mqttUser, mqttPass,
      lte_apn, mqttTopicEvents, mqttTopicCommands, mqttTopicCommandResponses);
    replyFunc(num, msg);
  }
  else if (cmd == "GET_MQTT_SERVER")
  {
    char msg[96];
    snprintf(msg, sizeof(msg), "MQTT_SERVER|%s|%u|%s",
             mqttServer, (unsigned)mqttPort, mqttServerIp.toString().c_str());
    replyFunc(num, msg);
  }
  else if (cmd == "GET_WIFI")
  {
    char msg[160];
    snprintf(msg, sizeof(msg), "WIFI|%s|%s|%d",
             WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(),
             (int)WiFi.RSSI());
    replyFunc(num, msg);
  }
  else if (cmd == "GET_SENSOR_LC")
  {
    char msg[768];
    int pos = 0;
    pos += snprintf(msg + pos, sizeof(msg) - pos, "SENSOR_LC");
    for (int s = 0; s < 2; s++) {
      const ChannelLC &lc = (s == 0) ? sensor1LC : sensor2LC;
      for (int ch = 0; ch < 4; ch++) {
        pos += snprintf(msg + pos, sizeof(msg) - pos,
                        "|s%dc%d:%.3f|%.3f|%u|%u",
                        s, ch,
                        lc.L[ch], lc.C[ch],
                        (unsigned)lc.conversion_time[ch],
                        (unsigned)lc.driver_current[ch]);
        if (pos >= (int)sizeof(msg) - 64) break;
      }
      if (pos >= (int)sizeof(msg) - 64) break;
    }
    replyFunc(num, msg);
  }
  else if (cmd == "GET_LOOP_CFG")
  {
    // Reuse sendAllSpeedPairConfigs to push CONFIG_ACK for each pair
    sendAllSpeedPairConfigs(num);
  }
  else if (cmd == "GET_TRAFFIC_RULES")
  {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "RULES_ACK|limit:%.1f|tol:%.1f|min_dist:%.1f|min_headway:%.2f|max_headway:%lu|straddle_ms:%lu|straddle_ratio:%.2f|assume_kmh:%.1f",
             g_traffic_rules.speed_limit_kmh,
             g_traffic_rules.speed_tolerance_kmh,
             g_traffic_rules.min_follow_distance_m,
             g_traffic_rules.min_headway_s,
             (unsigned long)g_traffic_rules.max_headway_ms,
             (unsigned long)g_traffic_rules.min_straddle_overlap_ms,
             g_traffic_rules.min_straddle_overlap_ratio,
             g_traffic_rules.assume_speed_kmh);
    replyFunc(num, msg);
  }
  else if (cmd == "GET_REPORT_CFG")
  {
    char msg[96];
    snprintf(msg, sizeof(msg), "REPORT_CFG|%d|%lu|%d",
             g_report_cfg.enabled ? 1 : 0,
             (unsigned long)g_report_cfg.interval_ms,
             g_report_cfg.periodic_clear ? 1 : 0);
    replyFunc(num, msg);
  }
  else if (cmd == "GET_DEFAULT_KMH")
  {
    DetectorConfig cfg = det[0][0].getConfig();
    char msg[64];
    snprintf(msg, sizeof(msg), "DEFAULT_KMH|%.2f", cfg.default_speed_kmh);
    replyFunc(num, msg);
  }
  else if (cmd == "SAVE_ALL")
  {
    bool ok = PersistentConfig::saveAllConfigs();
    replyFunc(num, ok ? "SAVE_ALL_ACK|ok" : "SAVE_ALL_ACK|partial_failure");
  }
}

// ============================================================
// WebSocket Event Handler
// ============================================================
void webSocketEvent(uint8_t num, uint8_t type, uint8_t *payload, size_t len)
{
  if (type != WS_TYPE_TEXT)
    return;
  String cmd = String((char *)payload, len);
  cmd.trim();

  processSystemCommand(cmd, wsSendToClient, num);
}