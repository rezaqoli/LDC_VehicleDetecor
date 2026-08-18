// ============================================================
// Globals.h  —  Shared extern declarations for all modules
// ============================================================
#pragma once

#include <Arduino.h>

class WebSocketsServer;

#include "Config.h"
#include "VehicleDetector.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "RAK12029_LDC1614.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ============================================================
// Loop Configuration & State (depends on EventResult)
// ============================================================
struct LoopConfig
{
  bool dualLoop = false;
  float distance = 2.0f;
  uint8_t sensor1 = 0;
  uint8_t ch1 = 0;
  uint8_t sensor2 = 1;
  uint8_t ch2 = 0;

  LoopConfig() = default;
  LoopConfig(bool enabled, float dist, uint8_t s1, uint8_t c1, uint8_t s2, uint8_t c2)
      : dualLoop(enabled), distance(dist), sensor1(s1), ch1(c1), sensor2(s2), ch2(c2) {}
};

struct SpeedPairState
{
  EventResult *e1 = nullptr;
  EventResult *e2 = nullptr;
  bool h1 = false;
  bool h2 = false;
  bool valid = false;
  float last_speed_kmh = 0.0f;
  float last_length_m = 0.0f;
  float last_delay_ms = 0.0f;
  uint32_t last_update_us = 0;
  char last_type[8] = "Unknown";
};

// ============================================================
// I2C Bus & Sensor Objects
// ============================================================
extern TwoWire I2C_Bus0;
extern TwoWire I2C_Bus1;
extern RAK12029_LDC1614_Inductive ldc1;
extern RAK12029_LDC1614_Inductive ldc2;

// ============================================================
// Sensor LC Configuration
// ============================================================
extern ChannelLC sensor1LC;
extern ChannelLC sensor2LC;

// ============================================================
// Vehicle Detector Array (2 sensors x 4 channels)
// ============================================================
extern VehicleDetector det[2][4];

// ============================================================
// Speed Pair Config & State
// ============================================================
extern LoopConfig loopCfg[SPEED_PAIR_COUNT];
extern SpeedPairState speedState[SPEED_PAIR_COUNT];

// ============================================================
// FreeRTOS Queues
// ============================================================
extern QueueHandle_t rawQueue;
extern QueueHandle_t eventQueue;
extern QueueHandle_t freeEventQueue;
extern QueueHandle_t wsTxQueue;
extern QueueHandle_t freeWsMsgQueue;

void releaseEventSlot(EventResult *slot);

// ============================================================
// FreeRTOS Semaphores
// ============================================================
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t i2c0Mutex;
extern SemaphoreHandle_t i2c1Mutex;
extern SemaphoreHandle_t wsMutex;

// ============================================================
// Event & WS Message Pools
// ============================================================
extern EventResult eventPool[EVENT_POOL_SIZE];
extern uint8_t eventPoolRefs[EVENT_POOL_SIZE];
extern WsTxMessage wsTxPool[WS_TX_POOL_SIZE];

// ============================================================
// Latest Sensor Data
// ============================================================
extern SensorDataSnapshot latestData;

// ============================================================
// Network Servers
// ============================================================
extern WebServer httpServer;
extern WebSocketsServer webSocket;

// ============================================================
// CPU Usage Tracking
// ============================================================
extern uint32_t cpu_usage_core0;
extern uint32_t cpu_usage_core1;
