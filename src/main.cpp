// ============================================================
// ESP32-S3  —  LDC1614 Vehicle Detection  v4.5 (Modular)
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer_Generic.h>

#include "Globals.h"

#include <Wire.h>
#include "RAK12029_LDC1614.h"
#include "dashboard_html.h"
#include <ESP2SOTA.h>

#include "SensorDriver.h"
#include "WsUtils.h"
#include "WsCommandHandler.h"
#include "Tasks.h"

// ============================================================
// Global Object Definitions
// ============================================================
TwoWire I2C_Bus0(0);
TwoWire I2C_Bus1(1);
RAK12029_LDC1614_Inductive ldc1(0x2A);
RAK12029_LDC1614_Inductive ldc2(0x2A);

ChannelLC sensor1LC = {{13, 13, 13, 13}, {330, 330, 330, 330}, {0x9C40, 0x9C40, 0x9C40, 0x9C40}, {0xA000, 0xA000, 0xA000, 0xA000}};
ChannelLC sensor2LC = {{13, 13, 13, 13}, {330, 330, 330, 330}, {0x9C40, 0x9C40, 0x9C40, 0x9C40}, {0xA000, 0xA000, 0xA000, 0xA000}};

VehicleDetector det[2][4] = {
    {VehicleDetector("S1C0"), VehicleDetector("S1C1"), VehicleDetector("S1C2"), VehicleDetector("S1C3")},
    {VehicleDetector("S2C0"), VehicleDetector("S2C1"), VehicleDetector("S2C2"), VehicleDetector("S2C3")}};

LoopConfig loopCfg[SPEED_PAIR_COUNT] = {
    {false, 0.4f, 0, 0, 1, 0},
    {false, 0.4f, 0, 1, 1, 1},
    {false, 0.4f, 0, 2, 1, 2},
    {false, 0.4f, 0, 3, 1, 3}};
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
  if (msg)
    webSocket.broadcastTXT(msg);
}

void wsLoop()
{
  webSocket.loop();
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
  Serial.println("\n[LDC1614 v4.5] Modular Build");

  pinMode(ESP_RUN_LED, OUTPUT);
  digitalWrite(ESP_RUN_LED, LOW);

  I2C_Bus0.begin(SDA1, SCL1, 400000);
  I2C_Bus1.begin(SDA2, SCL2, 400000);
  ldc1.LDC1614_reset_sensor(I2C_Bus0);
  ldc2.LDC1614_reset_sensor(I2C_Bus1);
  configureSensor(I2C_Bus0, ldc1, sensor1LC);
  configureSensor(I2C_Bus1, ldc2, sensor2LC);
  latestData.valid = false;
  for (uint8_t i = 0; i < SPEED_PAIR_COUNT; i++)
  {
    strncpy(speedState[i].last_type, "Unknown", sizeof(speedState[i].last_type) - 1);
    speedState[i].last_type[sizeof(speedState[i].last_type) - 1] = '\0';
  }

  //WiFi.mode(WIFI_STA);
  //WiFi.softAP("ESP-AP", NULL);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  Serial.printf("[WiFi] http://%s\n", WiFi.localIP().toString().c_str());

  httpServer.on("/", []
                { httpServer.send(200, "text/html; charset=utf-8", DASHBOARD_HTML); });
  httpServer.begin();
  ESP2SOTA.begin(&httpServer);
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  rawQueue = xQueueCreate(16, sizeof(RawFrame));
  eventQueue = xQueueCreate(EVENT_POOL_SIZE, sizeof(EventResult *));
  freeEventQueue = xQueueCreate(EVENT_POOL_SIZE, sizeof(EventResult *));
  wsTxQueue = xQueueCreate(WS_TX_POOL_SIZE, sizeof(WsTxMessage *));
  freeWsMsgQueue = xQueueCreate(WS_TX_POOL_SIZE, sizeof(WsTxMessage *));
  i2c0Mutex = xSemaphoreCreateMutex();
  i2c1Mutex = xSemaphoreCreateMutex();
  wsMutex = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();
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

  if (xTaskCreatePinnedToCore(taskSensorReading, "Sensor", 8192, NULL, 1, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskDetector, "Detector", 12288, NULL, 1, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskWsLoop, "WS", 12288, NULL, 2, NULL, 1) != pdPASS ||
      xTaskCreatePinnedToCore(taskSpeedMatch, "Speed", 12288, NULL, 2, NULL, 0) != pdPASS ||
      xTaskCreatePinnedToCore(taskWebServer, "HTTP", 8192, NULL, 3, NULL, 1) != pdPASS)
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
