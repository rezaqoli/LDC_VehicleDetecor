// ============================================================
// WsUtils.cpp  —  WebSocket utilities, speed pair config helpers
// ============================================================
#include "WsUtils.h"
#include <stdarg.h>

// ============================================================
// WebSocket TX (pooled, non-blocking)
// ============================================================
void wsSend(const char *msg)
{
  if (!msg || !wsTxQueue || !freeWsMsgQueue)
    return;

  WsTxMessage *slot = nullptr;
  if (xQueueReceive(freeWsMsgQueue, &slot, 0) != pdTRUE || !slot)
    return;

  snprintf(slot->text, sizeof(slot->text), "%s", msg);
  if (xQueueSend(wsTxQueue, &slot, 0) != pdTRUE)
  {
    xQueueSend(freeWsMsgQueue, &slot, 0);
  }
}

void sendCombinedWebSocketData(const SensorDataSnapshot &data)
{
  static char buffer[512];
  int pos = 0;
  bool truncated = false;

  auto append = [&](const char *fmt, ...) -> bool
  {
    if (pos >= (int)sizeof(buffer))
      return false;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + pos, sizeof(buffer) - pos, fmt, args);
    va_end(args);

    if (written < 0)
    {
      buffer[sizeof(buffer) - 1] = '\0';
      return false;
    }

    if (written >= (int)(sizeof(buffer) - pos))
    {
      pos = sizeof(buffer) - 1;
      buffer[pos] = '\0';
      return false;
    }

    pos += written;
    return true;
  };

  truncated = !append("SENSOR_DATA|");

  truncated = truncated || !append("%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu|",
                                   (unsigned long)data.filtered1[0],
                                   (unsigned long)data.filtered1[1],
                                   (unsigned long)data.filtered1[2],
                                   (unsigned long)data.filtered1[3],
                                   (unsigned long)data.filtered2[0],
                                   (unsigned long)data.filtered2[1],
                                   (unsigned long)data.filtered2[2],
                                   (unsigned long)data.filtered2[3]);
  if (truncated)
    return;

  wsBroadcast(buffer);
}

// ============================================================
// Loop/Channel ID Formatting
// ============================================================
void formatLoopChannelId(uint8_t sensor, uint8_t ch, char *out, size_t outSize)
{
  if (!out || outSize == 0)
    return;

  out[0] = '\0';
  if (outSize < 6)
    return;

  snprintf(out, outSize, "S%dC%d", sensor + 1, ch);
}

bool isChannelInEnabledSpeedPair(const char *channelId)
{
  for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
  {
    if (!loopCfg[i].dualLoop)
      continue;

    char id1[8], id2[8];
    formatLoopChannelId(loopCfg[i].sensor1, loopCfg[i].ch1, id1, sizeof(id1));
    formatLoopChannelId(loopCfg[i].sensor2, loopCfg[i].ch2, id2, sizeof(id2));
    if (strcmp(channelId, id1) == 0 || strcmp(channelId, id2) == 0)
      return true;
  }
  return false;
}

void refreshDetectorLoopModes()
{
  for (int s = 0; s < 2; s++)
  {
    for (int ch = 0; ch < 4; ch++)
    {
      det[s][ch].setDualLoopMode(isChannelInEnabledSpeedPair(det[s][ch].id()));
    }
  }
}

// ============================================================
// Speed Pair Configuration Management
// ============================================================
void applySpeedPairConfig(uint8_t idx, bool enabled, float distance,
                          uint8_t sensor1, uint8_t ch1, uint8_t sensor2, uint8_t ch2)
{
  if (idx >= SPEED_PAIR_COUNT)
    return;

  loopCfg[idx].dualLoop = enabled;
  loopCfg[idx].distance = (distance > 0.0f) ? distance : loopCfg[idx].distance;
  loopCfg[idx].sensor1 = constrain((int)sensor1, 0, 1);
  loopCfg[idx].ch1 = constrain((int)ch1, 0, 3);
  loopCfg[idx].sensor2 = constrain((int)sensor2, 0, 1);
  loopCfg[idx].ch2 = constrain((int)ch2, 0, 3);
  speedState[idx].h1 = false;
  speedState[idx].h2 = false;
  refreshDetectorLoopModes();
}

void sendSpeedPairConfig(uint8_t num, uint8_t idx)
{
  if (idx >= SPEED_PAIR_COUNT)
    return;

  char id1[8], id2[8], msg[192];
  LoopConfig &cfgPair = loopCfg[idx];
  formatLoopChannelId(cfgPair.sensor1, cfgPair.ch1, id1, sizeof(id1));
  formatLoopChannelId(cfgPair.sensor2, cfgPair.ch2, id2, sizeof(id2));
  snprintf(msg, sizeof(msg),
           "CONFIG_ACK|idx:%u|dual:%d|dist:%.2f|s1:%u|c1:%u|s2:%u|c2:%u|a:%s|b:%s",
           idx,
           cfgPair.dualLoop ? 1 : 0,
           cfgPair.distance,
           cfgPair.sensor1,
           cfgPair.ch1,
           cfgPair.sensor2,
           cfgPair.ch2,
           id1,
           id2);
  wsSendToClient(num, msg);
}

void sendAllSpeedPairConfigs(uint8_t num)
{
  for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
    sendSpeedPairConfig(num, i);
}

void sendAllSpeedResults(uint8_t num)
{
  for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
  {
    LoopConfig &cfgPair = loopCfg[i];
    SpeedPairState &st = speedState[i];
    char id1[8], id2[8], msg[224];
    formatLoopChannelId(cfgPair.sensor1, cfgPair.ch1, id1, sizeof(id1));
    formatLoopChannelId(cfgPair.sensor2, cfgPair.ch2, id2, sizeof(id2));
    snprintf(msg, sizeof(msg),
             "SPEED_STATE|idx:%u|valid:%d|speed:%.1f|len:%.2f|type:%s|delay:%.2f|dist:%.2f|dual:%d|a:%s|b:%s|ts:%lu",
             i,
             st.valid ? 1 : 0,
             st.last_speed_kmh,
             st.last_length_m,
             st.last_type,
             st.last_delay_ms,
             cfgPair.distance,
             cfgPair.dualLoop ? 1 : 0,
             id1,
             id2,
             (unsigned long)st.last_update_us);
    wsSendToClient(num, msg);
  }
}
