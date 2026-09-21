// ============================================================
// Tasks.cpp  —  FreeRTOS task implementations
// ============================================================
#include "Tasks.h"
#include "SensorDriver.h"
#include "WsUtils.h"
#include "esp_timer.h"
#include "TrafficStats.h"
#include "DetectionControl.h"
#include "TrafficMonitor.h"
#include "MqttHandler.h"
#include "OtaUpdater.h"
#include <ESPmDNS.h>

const u8_t LedSensors[8] = {LEDs1, LEDs2, LEDs3, LEDs4, LEDs5, LEDs6, LEDs7, LEDs8 };
// ============================================================
// Internal helpers (file-scoped)
// ============================================================
static EventResult *allocEventSlot()
{
  EventResult *slot = nullptr;
  if (freeEventQueue && xQueueReceive(freeEventQueue, &slot, 0) == pdTRUE)
  {
    if (slot >= eventPool && slot < eventPool + EVENT_POOL_SIZE)
      eventPoolRefs[slot - eventPool] = 1;
    return slot;
  }
  return nullptr;
}

static void retainEventSlot(EventResult *slot)
{
  if (slot && slot >= eventPool && slot < eventPool + EVENT_POOL_SIZE)
    eventPoolRefs[slot - eventPool]++;
}

void releaseEventSlot(EventResult *slot)
{
  if (slot && freeEventQueue)
  {
    if (slot >= eventPool && slot < eventPool + EVENT_POOL_SIZE)
    {
      uint8_t &refs = eventPoolRefs[slot - eventPool];
      if (refs > 1)
      {
        refs--;
        return;
      }
      refs = 0;
    }
    xQueueSend(freeEventQueue, &slot, 0);
  }
}

static void onRecalibrate(const char *id)
{
  char msg[64];
  snprintf(msg, sizeof(msg), "RECALIBRATE_NEEDED|%s", id);
  wsSend(msg);
}

// ============================================================
// Task: Sensor Reading (core 0, 8192 stack)
// ============================================================
void taskSensorReading(void *)
{
  TickType_t wake = xTaskGetTickCount();
  while (true)
  {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLING_MS));
    RawFrame frame = {};
    for (int s = 0; s < 2; s++)
      for (int ch = 0; ch < 4; ch++)
        frame.filtered[s][ch] = 0;
    frame.ts_us = esp_timer_get_time();
    #ifdef ESP32s3
    readSensorChannels<TwoWire>(I2C_Bus0, i2c0Mutex, ldc1, frame, 0);
    readSensorChannels<TwoWire>(I2C_Bus1, i2c1Mutex, ldc2, frame, 1);
    #else
    readSensorChannels<SoftWire>(I2C_Bus0, i2c0Mutex, ldc1, frame, 0);
    readSensorChannels<SoftWire>(I2C_Bus1, i2c1Mutex, ldc2, frame, 1);
    #endif

    if (rawQueue)
      xQueueSend(rawQueue, &frame, 0);
  }
}

void ledBlink(u8_t pin, uint32_t interval_ms)
{
  static uint32_t last_toggle_ms[8] = {0,0,0,0,0,0,0,0};
  uint32_t now = esp_timer_get_time() / 1000; // Convert to milliseconds
  if (now - last_toggle_ms[pin] >= interval_ms)
  {
    digitalWrite(LedSensors[pin], !digitalRead(LedSensors[pin]));
    last_toggle_ms[pin] = now;
  }
}

// ============================================================
// Task: Detector (core 0, 12288 stack)
// ============================================================
void taskDetector(void *)
{
  RawFrame frame;
  static EventResult ev;
  static uint32_t ledOffTime[8] = {0,0,0,0,0,0,0,0};
  static bool ledActive[8] = {0,0,0,0,0,0,0,0};

  for (int s = 0; s < 2; s++)
    for (int ch = 0; ch < 4; ch++)
    {
      det[s][ch].onRecalibrateNeeded = onRecalibrate;
      det[s][ch].startCalibration();
    }

  while (true)
  {
    if (xQueueReceive(rawQueue, &frame, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      if (g_detectionPaused)
      {
        // Paused: drop the frame so the queue doesn't grow.
        continue;
      }
      for (int s = 0; s < 2; s++)
      {
        for (int ch = 0; ch < 4; ch++)
        {
          uint32_t val = frame.filtered[s][ch];
          if (val == 0)
            continue;
          if (det[s][ch].feed(val, frame.ts_us, ev))
          {
            trafficMonitorOnRawEvent(ev);

            if (det[s][ch].isDualLoopMode())
            {
              EventResult *slot = allocEventSlot();
              if (slot)
              {
                *slot = ev;
                if (xQueueSend(eventQueue, &slot, 0) != pdTRUE)
                  releaseEventSlot(slot);
              }
            }
            else
            {
              reportEvent(ev, wsSend);
              trafficMonitorOnSingleEvent(ev);
              ledOffTime[s*4 + ch] = esp_timer_get_time() / 1000 + 500; // Convert to milliseconds + 500ms blink
              digitalWrite(LedSensors[s*4 + ch], HIGH);
              ledActive[s*4 + ch ] = true;
            }
          }
        }
      }

      // Update latestData snapshot
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE)
      {
        for (int ch = 0; ch < 4; ch++)
        {
          latestData.filtered1[ch] = frame.filtered[0][ch];
          latestData.filtered2[ch] = frame.filtered[1][ch];
          latestData.mean1[ch] = det[0][ch].baseline();
          latestData.mean2[ch] = det[1][ch].baseline();
          latestData.stdDev1[ch] = det[0][ch].noiseStd();
          latestData.stdDev2[ch] = det[1][ch].noiseStd();
          latestData.anomalyScore1[ch] = 0;
          latestData.anomalyScore2[ch] = 0;
          strncpy(latestData.status1[ch], "OK", sizeof(latestData.status1[ch]) - 1);
          latestData.status1[ch][sizeof(latestData.status1[ch]) - 1] = '\0';
          strncpy(latestData.status2[ch], "OK", sizeof(latestData.status2[ch]) - 1);
          latestData.status2[ch][sizeof(latestData.status2[ch]) - 1] = '\0';
        }
        latestData.valid = true;
        xSemaphoreGive(dataMutex);
      }
    }

    // LED blink (2s toggle)
    static uint32_t last_toggle_us = 0;
    uint32_t now = esp_timer_get_time() / 1000;
    if (now - last_toggle_us >= 2000)
    {
      digitalWrite(ESP_RUN_LED, !digitalRead(ESP_RUN_LED));
      last_toggle_us = now;
    }

    for(u8_t i = 0 ; i<8 ; i++)
    {
      if(ledActive[i] & now >= ledOffTime[i])
      {
        digitalWrite(LedSensors[i] , LOW);
        ledActive[i] = false;
      }
    }
    

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// Task: Speed Match (core 0, 12288 stack)
// ============================================================
void taskSpeedMatch(void *)
{
  while (true)
  {
    EventResult *ev = nullptr;
    if (xQueueReceive(eventQueue, &ev, pdMS_TO_TICKS(20)) == pdTRUE)
    {
      if (g_detectionPaused)
      {
        // Paused: don't process events; release the slot and skip.
        releaseEventSlot(ev);
        continue;
      }
      uint8_t matchCount = 0;
      for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
      {
        LoopConfig &cfgPair = loopCfg[i];
        SpeedPairState &st = speedState[i];
        if (!cfgPair.dualLoop)
          continue;

        char id1[8], id2[8];
        formatLoopChannelId(cfgPair.sensor1, cfgPair.ch1, id1, sizeof(id1));
        formatLoopChannelId(cfgPair.sensor2, cfgPair.ch2, id2, sizeof(id2));

        if (ev && strcmp(ev->channel_id, id1) == 0)
        {
          if (st.e1)
            releaseEventSlot(st.e1);
          if (matchCount > 0)
            retainEventSlot(ev);
          st.e1 = ev;
          st.h1 = true;
          matchCount++;
        }
        if (ev && strcmp(ev->channel_id, id2) == 0)
        {
          if (st.e2)
            releaseEventSlot(st.e2);
          if (matchCount > 0)
            retainEventSlot(ev);
          st.e2 = ev;
          st.h2 = true;
          matchCount++;
        }
      }
      if (matchCount == 0)
        releaseEventSlot(ev);
    }

    for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
    {
      LoopConfig &cfgPair = loopCfg[i];
      SpeedPairState &st = speedState[i];
      if (!cfgPair.dualLoop || !st.h1 || !st.h2 || !st.e1 || !st.e2)
        continue;

      char id1[8], id2[8];
      formatLoopChannelId(cfgPair.sensor1, cfgPair.ch1, id1, sizeof(id1));
      formatLoopChannelId(cfgPair.sensor2, cfgPair.ch2, id2, sizeof(id2));

      if (strcmp(id1, id2) == 0)
      {
        releaseEventSlot(st.e1);
        releaseEventSlot(st.e2);
        st.e1 = nullptr;
        st.e2 = nullptr;
        st.h1 = st.h2 = false;
        continue;
      }

      uint32_t event_delta_us = (st.e1->end_us > st.e2->end_us) ? (st.e1->end_us - st.e2->end_us) : (st.e2->end_us - st.e1->end_us);
      if (event_delta_us > 3000000UL)
      {
        releaseEventSlot(st.e1);
        releaseEventSlot(st.e2);
        st.e1 = nullptr;
        st.e2 = nullptr;
        st.h1 = st.h2 = false;
        continue;
      }

      float delay_ms = event_delta_us / 1000.0f;
      float abs_delay_ms = fabsf(delay_ms);

      if (abs_delay_ms > 0.05f && abs_delay_ms < 1000.0f)
      {
        float speed_ms = cfgPair.distance / (abs_delay_ms / 1000.0f);

        float length = speed_ms * ((st.e1->duration_ms + st.e2->duration_ms) * 0.5f / 1000.0f);
        st.e1->estimated_length_m = length;
        st.e2->estimated_length_m = length;
        det[cfgPair.sensor1][cfgPair.ch1].reclassify(*st.e1);
        det[cfgPair.sensor2][cfgPair.ch2].reclassify(*st.e2);
        const char *type = st.e1->vehicle_class;

        st.valid = true;
        st.last_speed_kmh = speed_ms * 3.6f;
        st.last_length_m = length;
        st.last_delay_ms = delay_ms;
        st.last_update_us = esp_timer_get_time();
        strncpy(st.last_type, type, sizeof(st.last_type) - 1);
        st.last_type[sizeof(st.last_type) - 1] = '\0';

        trafficMonitorOnDualMatch(i, st.e1, st.e2, st.last_speed_kmh);

        reportEvent(*st.e1, wsSend);
        reportEvent(*st.e2, wsSend);

        char msg[192];
        snprintf(msg, sizeof(msg),
                 "SPEED|idx:%u|speed:%.1f|len:%.2f|type:%s|delay:%.2f|dist:%.2f|a:%s|b:%s",
                 i, st.last_speed_kmh, st.last_length_m, st.last_type, st.last_delay_ms, cfgPair.distance, id1, id2);
        wsSend(msg);
        #ifdef ENABLE_MQTT
          mqttPublishEvent(msg);
        #endif
        Serial.println(msg);
      }

      releaseEventSlot(st.e1);
      releaseEventSlot(st.e2);
      st.e1 = nullptr;
      st.e2 = nullptr;
      st.h1 = st.h2 = false;
    }
    vTaskDelay(1);
  }
}

// ============================================================
// Task: WebServer (core 1, 8192 stack)
// ============================================================
void taskWebServer(void *)
{
  bool mdnsStarted = false;
  while (true)
  {
    if (!mdnsStarted && WiFi.status() == WL_CONNECTED)
    {
      if (MDNS.begin(DEVICE_HOSTNAME))
      {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        mdnsStarted = true;
        Serial.printf("[WiFi] mDNS active: http://%s.local/\n", DEVICE_HOSTNAME);
      }
      else
      {
        Serial.printf("[WiFi] mDNS start failed for %s.local\n", DEVICE_HOSTNAME);
      }
    }
    // ESP2SOTA and modem OTA both use Arduino Update's single global writer.
    // Do not allow a Wi-Fi /update request to call Update.begin/write/end while
    // the modem OTA task is flashing the application partition.
    if (!otaIsRunning())
      httpServer.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// Task: WebSocket Loop (core 1, 12288 stack)
// ============================================================
void taskWsLoop(void *)
{
  TickType_t lastSensorSend = xTaskGetTickCount();
  const TickType_t sensorPeriod = pdMS_TO_TICKS(50);

  while (true)
  {
    wsLoop();

    WsTxMessage *msg = nullptr;
    for (uint8_t i = 0; i < 4 && xQueueReceive(wsTxQueue, &msg, 0) == pdTRUE; i++)
    {
      if (msg)
      {
        wsBroadcast(msg->text);
        xQueueSend(freeWsMsgQueue, &msg, 0);
      }
    }

    TickType_t now = xTaskGetTickCount();
    if (dataStreamOnWs && (now - lastSensorSend) >= sensorPeriod)
    {
      lastSensorSend = now;
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(2)) == pdTRUE)
      {
        if (latestData.valid)
        {
          SensorDataSnapshot snapshot = latestData;
          xSemaphoreGive(dataMutex);
          sendCombinedWebSocketData(snapshot);
        }
        else
        {
          xSemaphoreGive(dataMutex);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ============================================================
// Task: Traffic Stats Reporter (core 1, 4096 stack)
// ============================================================
void taskStatsReporter(void *)
{
  while (true)
  {
    trafficStatsPeriodic();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
