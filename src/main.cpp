// ============================================================
// ESP32-S3  —  LDC1614 Vehicle Detection  v7.0 (Modular)
// ============================================================
#include <Arduino.h>
#include "PersistentConfig.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer_Generic.h>
#include <SoftWire.h>

#include "config.h"
#include "Globals.h"

#include <Wire.h>
#include "RAK12029_LDC1614.h"
#include "dashboard_html.h"
#include "dashboard_tech.h"
#include "dashboard_mqtt.h"
#include "dashboard_setup.h"
#include <ESP2SOTA.h>

#include "SensorDriver.h"
#include "WsUtils.h"
#include "WsCommandHandler.h"
#include "Tasks.h"
#include "LteModem.h"
#include "MqttHandler.h"
#include "TrafficStats.h"
#include "TrafficMonitor.h"
#include "LoopGeometry.h"
#include "PowerMonitor.h"
#include "TimeManager.h"
#include "OtaUpdater.h"
#include "DetectionControl.h"
#include "SmsManager.h"
// ============================================================
// Global Object Definitions
// ============================================================
#ifdef ESP32s3
  TwoWire I2C_Bus0(0);
  TwoWire I2C_Bus1(1);
#else
  #ifdef ESP32
  SoftWire I2C_Bus0(SDA, SCL);
  SoftWire I2C_Bus1(SDA2, SCL2);
  char swTxBuffer0[16];
  char swRxBuffer0[16];
  char swTxBuffer1[16];
  char swRxBuffer1[16];
  #define SW_TIMEOUT 40
  #endif
#endif


RAK12029_LDC1614_Inductive ldc1(0x2A);
RAK12029_LDC1614_Inductive ldc2(0x2A);

ChannelLC sensor1LC = {{13, 13, 13, 13}, {330, 330, 330, 330}, {0x9C40, 0x9C40, 0x9C40, 0x9C40}, {0xA000, 0xA000, 0xA000, 0xA000}};
ChannelLC sensor2LC = {{13, 13, 13, 13}, {330, 330, 330, 330}, {0x9C40, 0x9C40, 0x9C40, 0x9C40}, {0xA000, 0xA000, 0xA000, 0xA000}};

VehicleDetector det[2][4] = {
    {VehicleDetector("S1C0"), VehicleDetector("S1C1"), VehicleDetector("S1C2"), VehicleDetector("S1C3")},
    {VehicleDetector("S2C0"), VehicleDetector("S2C1"), VehicleDetector("S2C2"), VehicleDetector("S2C3")}};

LoopConfig loopCfg[SPEED_PAIR_COUNT] = {
    {false, 0.4f, 0, 0, 0, 1},
    {false, 0.4f, 0, 2, 0, 3},
    {false, 0.4f, 1, 0, 1, 1},
    {false, 0.4f, 1, 2, 1, 3}};
SpeedPairState speedState[SPEED_PAIR_COUNT];

SensorDataSnapshot latestData;

// ============================================================
// FreeRTOS Queues & Semaphores
// ============================================================
QueueHandle_t rawQueue;
QueueHandle_t eventQueue;
QueueHandle_t freeEventQueue;
QueueHandle_t wsTxQueue;
QueueHandle_t freeWsMsgQueue;
SemaphoreHandle_t i2c0Mutex, i2c1Mutex, wsMutex;
SemaphoreHandle_t dataMutex;
volatile bool dataStreamOnWs = true;

EventResult eventPool[EVENT_POOL_SIZE];
uint8_t eventPoolRefs[EVENT_POOL_SIZE];
WsTxMessage wsTxPool[WS_TX_POOL_SIZE];

// ============================================================
// Network Servers
// ============================================================
WebServer httpServer(80);
WebSocketsServer webSocket(81);

const char* WIFI_SSID = "Akhtarniroo";
const char* WIFI_PASSWORD = "@esp8266!";

void wsSendToClient(uint8_t num, const char *msg)
{
  if (msg)
    webSocket.sendTXT(num, msg);
}

void wsBroadcast(const char *msg)
{
  if (!msg)
    return;

  if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    webSocket.broadcastTXT(msg);
    xSemaphoreGive(wsMutex);
  }
}

void sendTrafficReport(const char *msg)
{
  if (!msg)
    return;

  wsBroadcast(msg);
  #ifdef ENABLE_MQTT
    mqttPublishEvent(msg);
  #endif
}

void wsLoop()
{
  webSocket.loop();
}

static bool requireDashboardAuthentication()
{
  if (httpServer.authenticate(DASHBOARD_USER, DASHBOARD_PASSWORD))
    return true;

  httpServer.requestAuthentication(BASIC_AUTH, "LDC Dashboard");
  return false;
}

// ============================================================
// CPU Usage
// ============================================================
uint32_t cpu_usage_core0 = 0;
uint32_t cpu_usage_core1 = 0;



// ============================================================
// Setup
// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[LDC1614 v5.6] Modular Build");

  // Initialize persistent storage FIRST
  PersistentConfig::init();
  // Load MQTT Client ID from NVS (or use default if not set)
  PersistentConfig::getMqttClientId(mqttClientId, sizeof(mqttClientId));
  Serial.printf("[CFG] MQTT Client ID: %s\n", mqttClientId);

  // Pre-create the modem mutex so other tasks can takeModem() safely
  // before taskLTEInit() runs.
  modemMutexInit();

  PersistentConfig::loadAllConfigs();
  Serial.println("[NVS] All config loaded from storage");

  #ifdef ESP32s3
  pinMode(ESP_RUN_LED, OUTPUT);
  digitalWrite(ESP_RUN_LED, LOW);
  I2C_Bus0.begin(SDA1, SCL1, 400000);
  I2C_Bus1.begin(SDA2, SCL2, 400000);
  
  //#elifdef ESP32
  #else
  #ifdef ESP32
  Serial.println("\n 1");
  I2C_Bus0.setTxBuffer(swTxBuffer0, sizeof(swTxBuffer0));
  I2C_Bus0.setRxBuffer(swRxBuffer0, sizeof(swRxBuffer0));
  I2C_Bus0.setTimeout_ms(1000);
  I2C_Bus0.setDelay_us(5);
  Serial.println("\n 3");
  I2C_Bus0.begin();
  I2C_Bus1.setTxBuffer(swTxBuffer1, sizeof(swTxBuffer1));
  I2C_Bus1.setRxBuffer(swRxBuffer1, sizeof(swRxBuffer1));
  I2C_Bus1.setTimeout_ms(1000);
  I2C_Bus1.setDelay_us(5);
  Serial.println("\n 2");
  I2C_Bus1.begin();
  Serial.println("\n 4");
  #endif
  #endif

  ldc1.LDC1614_reset_sensor(I2C_Bus0);
  ldc2.LDC1614_reset_sensor(I2C_Bus1);
  #ifndef ESP32s3
    configureSensor<SoftWire>(I2C_Bus0, ldc1, sensor1LC);
    configureSensor<SoftWire>(I2C_Bus1, ldc2, sensor2LC);
  #else
    configureSensor<TwoWire>(I2C_Bus0, ldc1, sensor1LC);
    configureSensor<TwoWire>(I2C_Bus1, ldc2, sensor2LC);
  #endif

  latestData.valid = false;
  for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
  {
    strncpy(speedState[i].last_type, "Unknown", sizeof(speedState[i].last_type) - 1);
    speedState[i].last_type[sizeof(speedState[i].last_type) - 1] = '\0';
  }

  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP-AP", NULL);
  IPAddress ip(192, 168, 100, 232);
  IPAddress gateway(192, 168, 100, 1);
  IPAddress subnet(255, 255, 255, 0);
  //WiFi.config(ip, gateway, subnet);
  
  //WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  Serial.printf("[WiFi] http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] mDNS hostname: %s.local\n", DEVICE_HOSTNAME);

  httpServer.on("/dev", []
                { if (requireDashboardAuthentication()) httpServer.send(200, "text/html; charset=utf-8", DASHBOARD_HTML); });
  httpServer.on("/tech", []
                { if (requireDashboardAuthentication()) httpServer.send(200, "text/html; charset=utf-8", DASHBOARD_TECH_HTML); });
  httpServer.on("/setup", []
                { if (requireDashboardAuthentication()) httpServer.send(200, "text/html; charset=utf-8", DASHBOARD_SETUP_HTML); });
  httpServer.on("/mqtt", []
                { if (requireDashboardAuthentication()) httpServer.send(200, "text/html; charset=utf-8", DASHBOARD_MQTT_HTML); });
  httpServer.on("/", []()
                {
                  const char *idx =
                    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>LDC Dashboards</title>"
                    "<style>body{font-family:Arial;margin:30px;}a{display:block;margin:8px 0;font-size:18px;}</style>"
                    "</head><body>"
                    "<h1>LDC1614 Dashboards</h1>"
                    "<a href='/dev'>/dev &mdash; Engineer dashboard (sensors, detector, classification)</a>"
                    "<a href='/tech'>/tech &mdash; Technician dashboard (system/MQTT/LC/loop config)</a>"
                    "<a href='/setup'>/setup &mdash; Device setup dashboard</a>"
                    "<a href='/mqtt'>/mqtt &mdash; MQTT monitor (live broker traffic & event log)</a>"
                    "</body></html>";
                  httpServer.send(200, "text/html; charset=utf-8", idx);
                });
  httpServer.begin();
  ESP2SOTA.begin(&httpServer);
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  mqttInit();

  rawQueue       = xQueueCreate(16, sizeof(RawFrame));
  eventQueue     = xQueueCreate(EVENT_POOL_SIZE, sizeof(EventResult *));
  freeEventQueue = xQueueCreate(EVENT_POOL_SIZE, sizeof(EventResult *));
  wsTxQueue      = xQueueCreate(WS_TX_POOL_SIZE, sizeof(WsTxMessage *));
  freeWsMsgQueue = xQueueCreate(WS_TX_POOL_SIZE, sizeof(WsTxMessage *));
  i2c0Mutex      = xSemaphoreCreateMutex();
  i2c1Mutex      = xSemaphoreCreateMutex();
  wsMutex        = xSemaphoreCreateMutex();
  dataMutex      = xSemaphoreCreateMutex();

  trafficStatsInit();
  trafficStatsSetReportSender(sendTrafficReport);
  // loadAllConfigs() has already restored geometry and speed-pair settings.
  // Do not overwrite those persisted values with defaults at boot.
  trafficMonitorSyncFromGeometry();
  refreshDetectorLoopModes();

  powerMonitorInit();
  gnssInit();
  smsInit();

  if (!rawQueue || !eventQueue || !freeEventQueue || !wsTxQueue || !freeWsMsgQueue || !i2c0Mutex || !i2c1Mutex || !wsMutex || !dataMutex)
  {
    Serial.println("[ERR] Failed to create queue/semaphore");
    while (1)
      ;
  }

  for (uint8_t i = 0; i < EVENT_POOL_SIZE; i++)
  {
    EventResult *slot = &eventPool[i];
    xQueueSend(freeEventQueue, &slot, 0);
  }

  for (uint8_t i = 0; i < WS_TX_POOL_SIZE; i++)
  {
    WsTxMessage *slot = &wsTxPool[i];
    xQueueSend(freeWsMsgQueue, &slot, 0);
  }
//xTaskCreatePinnedToCore(taskSensorReading, "Sensor", 8192, NULL, 1, NULL, 0) != pdPASS ||
// ||
//xTaskCreatePinnedToCore(taskMqttLoop,          "MQTT-Loop", 8192, NULL, 1, NULL, 1) != pdPASS ||
//xTaskCreatePinnedToCore(taskGnssIdleWatcher,  "GNSS",    3072, NULL, 1, NULL, 1) != pdPASS ||
//xTaskCreatePinnedToCore(taskLTECommandConsole, "LTE-Console", 4096, NULL, 5, NULL, 1) != pdPASS ||
//xTaskCreatePinnedToCore(taskSmsService,      "SMS",     4096, NULL, 1, NULL, 1) != pdPASS ||
  if (
      xTaskCreatePinnedToCore(taskMqttPublisher,    "MQTT-Pub",  8192, NULL, 5, NULL, 1) != pdPASS ||
      xTaskCreatePinnedToCore(taskSensorReading,        "Sensor", 8192, NULL, 2, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskDetector,          "Detector", 12288, NULL, 1, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskWsLoop,                   "WS", 12288, NULL, 3, NULL, 1) != pdPASS ||
      xTaskCreatePinnedToCore(taskSpeedMatch,           "Speed", 12288, NULL, 1, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskStatsReporter,        "Stats", 4096, NULL, 2, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskPowerMonitor,      "Power",   4096, NULL, 5, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskTimeSync,          "Time",    4096, NULL, 4, NULL, 0) != pdPASS ||     
      xTaskCreatePinnedToCore(taskLTEInit,            "LTE-Init", 8192, NULL, 3, NULL, 1) != pdPASS ||
      xTaskCreatePinnedToCore(taskLTEStatusMonitor, "LTE-Monitor", 4096, NULL, 6, NULL, 1) != pdPASS ||
      xTaskCreatePinnedToCore(taskWebServer,                "HTTP", 8192, NULL, 4, NULL, 1) != pdPASS
      )
  {
    Serial.println("[ERR] Failed to create task");
    while (1)
      ;
  }

  Serial.println("[OK] Ready — ESP-AP");
  Serial.println("Commands: CALIBRATE, GET_CONFIG, GET_CPU, GET_NOISE, SET_THRESHOLD|enter|value, ...");
}

void loop()
{
  vTaskDelay(portMAX_DELAY);
}
