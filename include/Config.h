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
  #define LEDs1       42
  #define LEDs2       41
  #define LEDs3       40
  #define LEDs4       39
  #define LEDs5       38
  #define LEDs6       37
  #define LEDs7       36
  #define LEDs8       35
  extern const u8_t  LedSensors[8];
  #define ENABLE_MQTT 1
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
extern char lte_apn[32];  // APN for LTE connection, can be overridden by NVS

// MQTT Configuration
extern char mqttClientId[32];   // Buffer for client ID, persisted in NVS
extern char mqttServer[64];     // Buffer for MQTT server address, persisted in NVS
extern IPAddress mqttServerIp;  // Resolved IP address of MQTT server
extern uint16_t mqttPort;  // Port for MQTT server, can be overridden by NVS
extern char mqttUser[32];  // MQTT username, can be overridden by NVS
extern char mqttPass[32];  // MQTT password, can be overridden by NVS
extern char mqttTopicEvents[64];  // Topic for publishing events
extern char mqttTopicCommands[64];  // Topic for receiving commands
extern char mqttTopicCommandResponses[64];  // Topic for command responses

// ============================================================
// Battery / Solar voltage sense (ESP32-S3 ADC1)
// ============================================================
#define BATTERY_PIN        1   // ADC1_CH0
#define SOLAR_PIN          2   // ADC1_CH1
#define VOLTAGE_DIVIDER_R1 150.0f
#define VOLTAGE_DIVIDER_R2 10.0f
#define BATTERY_LOW_V      9.0f
#define BATTERY_OK_V       9.5f
#define POWER_SAMPLE_AVG   32
#define POWER_PERIOD_MS    60000UL

// ============================================================
// Time sync
// ============================================================
#define TIME_TZ              "IRST-3:30"   // POSIX TZ string for localtime()
#define TIME_SYNC_PERIOD_MS  600000UL      // 10 minutes
#define TIME_SYNC_INITIAL_MS 30000UL       // wait for LTE to come up

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
