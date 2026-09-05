#include "MqttHandler.h"
#include "WsCommandHandler.h" // To reuse command parsing logic if desired
#include "LteModem.h"
#include <cstring>


PubSubClient mqttClient(lteClient);
char mqttClientId[32] = "ESP32_Vehicle_Detector";  // Default, overridden by NVS
char mqttServer[64] = "\0";//"iot.iolink.ir";  // Default
IPAddress mqttServerIp = IPAddress(37, 255, 236, 124);  // Resolved IP address of MQTT server
uint16_t mqttPort = 1010;  // Default port, can be overridden by NVS
char mqttUser[32] = "";  // MQTT username, can be overridden by NVS
char mqttPass[32] = "";  // MQTT password, can be overridden by NVS
char mqttTopicEvents[64] = "vehicles/events";
char mqttTopicCommands[64] = "vehicles/commands";
char mqttTopicCommandResponses[64] = "vehicles/command_responses";

// ============================================================
// Publish pipeline (queue + worker)
// ============================================================
#define MQTT_PUB_PAYLOAD_MAX 320
#define MQTT_PUB_QUEUE_LEN   16

typedef enum
{
  MQTT_PUB_KIND_EVENT = 0,
  MQTT_PUB_KIND_RESPONSE = 1,
} mqtt_pub_kind_t;

typedef struct
{
  uint8_t          kind;        // mqtt_pub_kind_t
  uint16_t         payload_len; // strlen(payload)
  char             payload[MQTT_PUB_PAYLOAD_MAX];
} mqtt_pub_item_t;

static QueueHandle_t        s_pubQueue       = nullptr;
static mqtt_pub_item_t      s_pubQueueStorage[MQTT_PUB_QUEUE_LEN];

// Diagnostics (single writer = publisher task; readers = WS handler)
volatile uint32_t mqttPubPublished = 0;
volatile uint32_t mqttPubDropped   = 0;
volatile uint32_t mqttPubReconnect = 0;
char             mqttPubLastErr[64] = "none";

void setLastErr(const char *s)
{
  if (!s) s = "none";
  strncpy(mqttPubLastErr, s, sizeof(mqttPubLastErr) - 1);
  mqttPubLastErr[sizeof(mqttPubLastErr) - 1] = '\0';
}

static void mqttPublishEventInternal(const char *payload);
static void mqttPublishResponseInternal(const char *payload);

// ============================================================
// Public: enqueue a publish request (non-blocking from caller's POV).
// Returns true if the request was queued, false if it was dropped.
// ============================================================
static bool enqueuePublish(mqtt_pub_kind_t kind, const char *payload)
{
  if (!payload || !*payload) return false;
  if (!s_pubQueue)
  {
    // Queue not initialised yet — fall back to a direct publish attempt.
    if (takeModem(500))
    {
      if (kind == MQTT_PUB_KIND_EVENT)      mqttPublishEventInternal(payload);
      else                                  mqttPublishResponseInternal(payload);
      giveModem();
      return true;
    }
    mqttPubDropped++;
    setLastErr("queue_not_ready");
    return false;
  }

  mqtt_pub_item_t item;
  item.kind = (uint8_t)kind;
  size_t n = strnlen(payload, MQTT_PUB_PAYLOAD_MAX - 1);
  memcpy(item.payload, payload, n);
  item.payload[n] = '\0';
  item.payload_len = (uint16_t)n;

  if (xQueueSend(s_pubQueue, &item, 0) != pdTRUE)
  {
    mqttPubDropped++;
    setLastErr("queue_full");
    Serial.printf("[MQTT] DROP: publish queue full (payload %u bytes)\n", (unsigned)n);
    return false;
  }
  return true;
}

void mqttInit()
{
    if (strlen(mqttServer) < 3)
        mqttClient.setServer(mqttServerIp, mqttPort);
    else
        mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setSocketTimeout(60);
    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    Serial.printf("[MQTT] Init: server=%s:%d, IP=%s, client=%s keepalive=%d buf=%d\n",
                  mqttServer, mqttPort, mqttServerIp.toString().c_str(), mqttClientId,
                  MQTT_KEEPALIVE, MQTT_MAX_PACKET_SIZE);

    // Create the publish queue.
    s_pubQueue = xQueueCreate(MQTT_PUB_QUEUE_LEN, sizeof(mqtt_pub_item_t));
    if (!s_pubQueue)
    {
      Serial.println("[MQTT] FATAL: failed to create publish queue");
      setLastErr("queue_create_failed");
    }
    else
    {
      Serial.printf("[MQTT] Publish queue ready (len=%d, payload_max=%d)\n",
                    MQTT_PUB_QUEUE_LEN, MQTT_PUB_PAYLOAD_MAX);
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("[MQTT] Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    String message = "";
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.println(message);

    // Route to the system command parser; the reply closure enqueues the
    // response on the publish queue so the caller's task is never blocked.
    auto reply = [](uint8_t num, const char *msg)
    { enqueuePublish(MQTT_PUB_KIND_EVENT, msg); };
    processSystemCommand(message, reply, length);
}

static bool mqttPublishLocked(const char *topic, const char *payload)
{
  if (!mqttClient.connected())
    return false;

  bool ok = mqttClient.publish(topic, payload);
  if (!ok)
  {
    Serial.printf("[MQTT] Publish FAILED to %s (len=%d)\n",
                  topic, payload ? (int)strlen(payload) : 0);
    setLastErr("publish_returned_false");
  }
  return ok;
}

bool mqttConnect()
{
  if (mqttClient.connected())
    return true;

  Serial.println("\n[MQTT] ========================================");
  Serial.printf("[MQTT] Attempting connection to: %s:%d\n", mqttServer, mqttPort);

  if (mqttClient.connect(mqttClientId, mqttUser, mqttPass))
  {
    Serial.println("[MQTT] SUCCESS: Connected to MQTT Broker!");
    mqttClient.subscribe(mqttTopicCommands);
    Serial.printf("[MQTT] Subscribed to: %s\n", mqttTopicCommands);
    Serial.println("[MQTT] ========================================\n");
    mqttPubReconnect++;
    setLastErr("ok");
    return true;
  }

  int state = mqttClient.state();
  Serial.printf("[MQTT] FAILED! State Code: %d\n", state);
  setLastErr("connect_failed");
  Serial.println("[MQTT] ========================================\n");
  return false;
}

static void mqttPublishEventInternal(const char *payload)
{
  if (!lteInitialized || !lteGprsConnected)
  {
    setLastErr("modem_down");
    return;
  }

  if (!mqttClient.connected())
    mqttConnect();

  if (mqttClient.connected())
  {
    if (mqttPublishLocked(mqttTopicEvents, payload))
    {
      Serial.printf("[MQTT] Published event (%d bytes)\n", (int)strlen(payload));
      mqttPubPublished++;
      setLastErr("ok");
    }
  }
  else
  {
    setLastErr("broker_not_connected");
  }
}

static void mqttPublishResponseInternal(const char *payload)
{
  if (!lteInitialized || !lteGprsConnected)
  {
    setLastErr("modem_down");
    return;
  }

  if (!mqttClient.connected())
    mqttConnect();

  if (mqttClient.connected())
  {
    if (mqttPublishLocked(mqttTopicCommandResponses, payload))
    {
      Serial.printf("[MQTT] Published response (%d bytes)\n", (int)strlen(payload));
      mqttPubPublished++;
      setLastErr("ok");
    }
  }
  else
  {
    setLastErr("broker_not_connected");
  }
}

// Public, non-blocking: enqueue. The actual TCP/MQTT work happens in
// taskMqttPublisher() so the caller (detector, WS, etc.) is never blocked.
void mqttPublishEvent(const char *payload)
{
  enqueuePublish(MQTT_PUB_KIND_EVENT, payload);
}

void mqttPublishResponse(const char *payload)
{
  enqueuePublish(MQTT_PUB_KIND_RESPONSE, payload);
}

// ============================================================
// Worker: drain the publish queue and pump the MQTT client.
// Runs on core 1, priority 2 (above detector, below WS).
// ============================================================
void taskMqttPublisher(void *)
{
  Serial.println("[MQTT] Publisher worker started");
  mqtt_pub_item_t item;
  TickType_t lastConnectAttempt = 0;

  for (;;)
  {
    // Drain the publish queue with a short blocking wait so we yield CPU.
    if (xQueueReceive(s_pubQueue, &item, pdMS_TO_TICKS(200)) == pdTRUE)
    {
      if (takeModem(2000))
      {
        if (item.kind == MQTT_PUB_KIND_EVENT)
          mqttPublishEventInternal(item.payload);
        else
          mqttPublishResponseInternal(item.payload);
        giveModem();
      }
      else
      {
        mqttPubDropped++;
        Serial.println("[MQTT] DROP: could not lock modem for queued publish");
        setLastErr("mutex_timeout_in_worker");
      }
    }

    // Opportunistic pump + periodic reconnect attempts.
    if (takeModem(500))
    {
      if (lteInitialized && lteGprsConnected)
      {
        if (!mqttClient.connected())
        {
          TickType_t now = xTaskGetTickCount();
          if ((now - lastConnectAttempt) >= pdMS_TO_TICKS(5000))
          {
            lastConnectAttempt = now;
            mqttConnect();
          }
        }
        else
        {
          mqttClient.loop();
        }
      }
      giveModem();
    }
  }
}

void taskMqttLoop(void *)
{
  // Legacy entry point retained for compatibility. The real MQTT work is
  // done by taskMqttPublisher; this task just performs a low-frequency
  // status pump so existing wiring does not need to change.
  TickType_t wake = xTaskGetTickCount();
  while (true)
  {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(30000));
    if (lteInitialized && lteGprsConnected && takeModem(500))
    {
      if (mqttClient.connected())
        mqttClient.loop();
      giveModem();
    }
  }
}
