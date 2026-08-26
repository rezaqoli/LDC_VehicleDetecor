// ============================================================
// Config.h  —  Hardware pins, constants, and shared data structures
// ============================================================
#pragma once
#include <Arduino.h>

#define ESP32s3 1
#ifdef ESP32s3 
// ============================================================
// Hardware Pin Definitions
// ============================================================
#define SDA1        9
#define SCL1        10
#define SDA2        12
#define SCL2        13
#define ESP_RUN_LED 21
#else
  #ifdef ESP32
  #define SDA1        13
  #define SCL1        9
  #define SDA2        11
  #define SCL2        6
  #define ESP_RUN_LED 7
  #endif
#endif

#define SerialAT Serial1
#define TINY_GSM_MODEM_BG96
#define TINY_GSM_RX_BUFFER 1024
#define TINY_GSM_YIELD_MS 2
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_DEBUG Serial

#define MODEM_RX_PIN 18
#define MODEM_TX_PIN 17
#define MODEM_RESET_PIN 8
#define MODEM_BAUD_RATE 115200
#define MODEM_APN "shatelmobile"

// MQTT Configuration
#define MQTT_SERVER "iot.iolink.ir"//"192.168.100.32" // Or your private broker IP
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "ESP32_Vehicle_Detector"
#define MQTT_TOPIC_EVENTS "vehicles/events"
#define MQTT_TOPIC_COMMANDS "vehicles/commands"
#define MQTT_USER "" // Leave empty if no auth
#define MQTT_PASS "" // Leave empty if no auth

// ============================================================
// Timing & Sampling
// ============================================================
static const int SAMPLING_MS = 5; // 200 Hz sampling rate

// ============================================================
// Pool & Queue Sizes
// ============================================================
static const uint8_t EVENT_POOL_SIZE = 8;
static const uint8_t WS_TX_POOL_SIZE = 16;
static const size_t  WS_TX_MSG_MAX   = 800;

// ============================================================
// Speed Pair Configuration
// ============================================================
static const uint8_t SPEED_PAIR_COUNT = 4;

// ============================================================
// Data Structures
// ============================================================

// Per-channel LC tuning parameters for LDC1614 sensor
struct ChannelLC
{
  float L[4];
  float C[4];
  uint16_t conversion_time[4];
  uint16_t driver_current[4];
};

// Raw sensor frame produced by the sampling task
struct RawFrame
{
  uint32_t filtered[2][4]; // [sensor][channel]
  uint32_t ts_us;          // timestamp in microseconds
};

// WebSocket TX message slot (pooled)
struct WsTxMessage
{
  char text[WS_TX_MSG_MAX];
};

// Snapshot of latest sensor data for WebSocket broadcast
struct SensorDataSnapshot
{
  uint32_t filtered1[4], filtered2[4];
  float mean1[4], stdDev1[4];
  float mean2[4], stdDev2[4];
  uint8_t anomalyScore1[4], anomalyScore2[4];
  char status1[4][8], status2[4][8];
  bool valid;
};
