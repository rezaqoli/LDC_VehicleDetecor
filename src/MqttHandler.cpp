#include "MqttHandler.h"
#include "WsCommandHandler.h" // To reuse command parsing logic if desired
#include "ReplyChannel.h"
#include "LteModem.h"
#include <cstring>


PubSubClient mqttClient(lteClient);
char mqttClientId[32] = "\0"; // No default — must be set via SET_MQTT_ID|...
char mqttServer[64] = "\0";//"iot.iolink.ir";  // Default
IPAddress mqttServerIp = IPAddress(37, 255, 236, 124);  // Resolved IP address of MQTT server
uint16_t mqttPort = 1010;  // Default port, can be overridden by NVS
char mqttUser[32] = "";  // MQTT username, can be overridden by NVS
char mqttPass[32] = "";  // MQTT password, can be overridden by NVS
char mqttTopicEvents[64] = "vehicles/events";
char mqttTopicCommands[64] = "vehicles/commands";
char mqttTopicCommandResponses[64] = "vehicles/command_responses";

// Per-board topic suffixes, rebuilt on every (re)connect from the current
// mqttClientId so each board gets its own lanes. When the id is missing they
// fall back to the legacy bare topics.
static char s_topicEventsBoard[96]    = "vehicles/events";
static char s_topicCommandsBoard[96]  = "vehicles/commands";
static char s_topicResponsesBoard[96] = "vehicles/command_responses";
static constexpr const char *s_topicResponsesGlobal = "vehicles/command_responses";
static char s_lastCmdId[24]           = "";
static bool s_boardIdAnnounced        = false;

// ============================================================
// Publish pipeline (queue + worker)
// ============================================================
#define MQTT_PUB_QUEUE_LEN   16

typedef enum
{
  MQTT_PUB_KIND_EVENT = 0,
  MQTT_PUB_KIND_RESPONSE = 1,
  MQTT_PUB_KIND_RESPONSE_TARGETED = 2,
} mqtt_pub_kind_t;

typedef struct
{
  uint8_t          kind;        // mqtt_pub_kind_t
  uint16_t         payload_len; // strlen(payload)
  char             topic[96];
  char             payload[MQTT_PUB_PAYLOAD_MAX];
} mqtt_pub_item_t;

static QueueHandle_t        s_pubQueue       = nullptr;
static mqtt_pub_item_t      s_pubQueueStorage[MQTT_PUB_QUEUE_LEN];

// Diagnostics (single writer = publisher task; readers = WS handler)
volatile uint32_t mqttPubPublished    = 0;
volatile uint32_t mqttPubPublishedEvt = 0;
volatile uint32_t mqttPubPublishedRsp = 0;
volatile uint32_t mqttPubDropped      = 0;
volatile uint32_t mqttPubReconnect    = 0;
char             mqttPubLastErr[64]   = "none";

void setLastErr(const char *s)
{
  if (!s) s = "none";
  strncpy(mqttPubLastErr, s, sizeof(mqttPubLastErr) - 1);
  mqttPubLastErr[sizeof(mqttPubLastErr) - 1] = '\0';
}

static void mqttPublishEventInternal(const char *payload);
static void mqttPublishResponseInternal(const char *payload);
static void mqttPublishResponseToInternal(const char *topic, const char *payload);

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
      if (kind == MQTT_PUB_KIND_EVENT)               mqttPublishEventInternal(payload);
      else if (kind == MQTT_PUB_KIND_RESPONSE)        mqttPublishResponseInternal(payload);
      giveModem();
      return true;
    }
    mqttPubDropped++;
    setLastErr("queue_not_ready");
    return false;
  }

  mqtt_pub_item_t item;
  item.kind = (uint8_t)kind;
  item.topic[0] = '\0';
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

// Helper: split a "CMD|<cmd_id>|<payload>" envelope into (cmd_id, payload).
// Anything else is returned as-is so legacy boards keep working.
static void unwrapCmdEnvelope(const String &in, String &cmdIdOut, String &bodyOut)
{
  cmdIdOut = "";
  bodyOut = in;
  if (in.startsWith("CMD|"))
  {
    String rest = in.substring(4);
    int sep = rest.indexOf('|');
    if (sep < 0)
    {
      cmdIdOut = rest;
      bodyOut = "";
    }
    else
    {
      cmdIdOut = rest.substring(0, sep);
      bodyOut = rest.substring(sep + 1);
    }
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

    // Unwrap optional CMD|<cmd_id>|<payload> envelope so the existing
    // processSystemCommand() keeps working unchanged. The cmd_id is sticky
    // for the duration of processSystemCommand so multi-reply commands
    // (GET_CONFIG, etc.) all carry the same correlation token.
    String cmdId;
    String body;
    unwrapCmdEnvelope(message, cmdId, body);
    strncpy(s_lastCmdId, cmdId.c_str(), sizeof(s_lastCmdId) - 1);
    s_lastCmdId[sizeof(s_lastCmdId) - 1] = '\0';

    // Route to the system command parser via the medium-aware reply channel.
    // Replies go to mqttTopicCommandResponses (handled by MqttReplyChannel).
    IReplyChannel &reply = mqttReplyChannel();
    processSystemCommand(body, reply, 0);

    // Clear the cmd_id after the command has been fully processed so the
    // next inbound command starts fresh.
    s_lastCmdId[0] = '\0';
}

// Used by MqttReplyChannel to prefix RSP|<cmd_id>| on outgoing replies.
const char *mqttLastCmdId() { return s_lastCmdId; }
void mqttClearLastCmdId()  { s_lastCmdId[0] = '\0'; }

bool mqttHasClientId()      { return mqttClientId[0] != '\0'; }
const char *mqttTopicEventsBoard()    { return s_topicEventsBoard; }
const char *mqttTopicCommandsBoard()  { return s_topicCommandsBoard; }
const char *mqttTopicResponsesBoard() { return s_topicResponsesBoard; }

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

  // Each board must identify itself with a unique id (SET_MQTT_ID|...) so
  // the dashboard can distinguish it from other boards. The MQTT broker
  // enforces unique client ids — refusing without one prevents accidental
  // board aliasing.
  if (mqttClientId[0] == '\0')
  {
    Serial.println("[MQTT] REFUSED: no MQTT client id configured.");
    Serial.println("[MQTT] Run: SET_MQTT_ID|<your-board-id>  then restart.");
    Serial.println("[MQTT] ========================================\n");
    setLastErr("no_client_id");
    return false;
  }

  // Rebuild per-board topic suffixes from the current mqttClientId so each
  // board gets its own publish/subscribe lanes. Falls back to legacy topics
  // if the id contains characters PubSubClient cannot use in topic names.
  if (strchr(mqttClientId, '/') || strchr(mqttClientId, '+') || strchr(mqttClientId, '#'))
  {
    snprintf(s_topicEventsBoard,    sizeof(s_topicEventsBoard),    "vehicles/events");
    snprintf(s_topicCommandsBoard,  sizeof(s_topicCommandsBoard),  "vehicles/commands");
    snprintf(s_topicResponsesBoard, sizeof(s_topicResponsesBoard), "vehicles/command_responses");
  }
  else
  {
    snprintf(s_topicEventsBoard,    sizeof(s_topicEventsBoard),    "vehicles/%s/events",          mqttClientId);
    snprintf(s_topicCommandsBoard,  sizeof(s_topicCommandsBoard),  "vehicles/%s/commands",        mqttClientId);
    snprintf(s_topicResponsesBoard, sizeof(s_topicResponsesBoard), "vehicles/%s/command_responses", mqttClientId);
  }
  s_boardIdAnnounced = false;

  if (mqttClient.connect(mqttClientId, mqttUser, mqttPass))
  {
    Serial.println("[MQTT] SUCCESS: Connected to MQTT Broker!");
    mqttClient.subscribe(s_topicCommandsBoard);
    mqttClient.subscribe(mqttTopicCommands); // Legacy global lane
    Serial.printf("[MQTT] Subscribed to: %s\n", s_topicCommandsBoard);
    Serial.printf("[MQTT] Subscribed to: %s\n", mqttTopicCommands);
    Serial.println("[MQTT] ========================================\n");
    mqttPubReconnect++;
    setLastErr("ok");

    // Immediately announce our board id so the dashboard can register us
    // even before the first real traffic event arrives.
    char announce[64];
    snprintf(announce, sizeof(announce), "MQTT_ID|%s", mqttClientId);
    mqttClient.publish(s_topicEventsBoard,    announce);
    mqttClient.publish(mqttTopicEvents,      announce);
    s_boardIdAnnounced = true;

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
    bool okPerBoard = mqttPublishLocked(s_topicEventsBoard, payload);
    bool okGlobal   = mqttPublishLocked(mqttTopicEvents,   payload);
    if (okPerBoard || okGlobal)
    {
      Serial.printf("[MQTT] Published event (%d bytes)\n", (int)strlen(payload));
      mqttPubPublishedEvt++;
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
    bool okPerBoard = mqttPublishLocked(s_topicResponsesBoard, payload);
    // Always publish the legacy/global response lane. This is required when a
    // command arrived on vehicles/commands and lets fleet tools receive the
    // response even when they do not know the board-specific topic yet.
    bool okGlobal   = mqttPublishLocked(s_topicResponsesGlobal, payload);
    bool okConfigured = okGlobal;
    if (mqttTopicCommandResponses[0] && strcmp(mqttTopicCommandResponses, s_topicResponsesGlobal) != 0)
      okConfigured = mqttPublishLocked(mqttTopicCommandResponses, payload);
    if (okPerBoard || okGlobal || okConfigured)
    {
      Serial.printf("[MQTT] Published response (%d bytes)\n", (int)strlen(payload));
      mqttPubPublishedRsp++;
      mqttPubPublished++;
      setLastErr("ok");
    }
  }
  else
  {
    setLastErr("broker_not_connected");
  }
}

static void mqttPublishResponseToInternal(const char *topic, const char *payload)
{
  if (!topic || !*topic || !payload || !*payload) return;
  if (!lteInitialized || !lteGprsConnected)
  {
    setLastErr("modem_down");
    return;
  }

  if (!mqttClient.connected())
    mqttConnect();

  if (mqttClient.connected())
  {
    if (mqttPublishLocked(topic, payload))
    {
      Serial.printf("[MQTT] Published response to %s (%d bytes)\n", topic, (int)strlen(payload));
      mqttPubPublishedRsp++;
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

// Publish a response to a single, caller-chosen topic (used by
// MqttReplyChannel to fan out wrapped + bare replies on different lanes
// without doubling traffic).
void mqttPublishResponseTo(const char *topic, const char *payload)
{
  if (!topic || !*topic || !payload || !*payload) return;
  mqtt_pub_item_t item;
  item.kind = (uint8_t)MQTT_PUB_KIND_RESPONSE_TARGETED;
  size_t n = strnlen(payload, MQTT_PUB_PAYLOAD_MAX - 1);
  memcpy(item.payload, payload, n);
  item.payload[n] = '\0';
  item.payload_len = (uint16_t)n;
  strncpy(item.topic, topic, sizeof(item.topic) - 1);
  item.topic[sizeof(item.topic) - 1] = '\0';

  if (!s_pubQueue || xQueueSend(s_pubQueue, &item, 0) != pdTRUE)
  {
    mqttPubDropped++;
    setLastErr("queue_full");
    Serial.printf("[MQTT] DROP: targeted publish queue full (topic=%s, payload %u bytes)\n", topic, (unsigned)n);
  }
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
    // While OTA holds the modem exclusively, sleep instead of hammering takeModem().
    if (otaIsModemExclusive())
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    // Drain the publish queue with a short blocking wait so we yield CPU.
    if (xQueueReceive(s_pubQueue, &item, pdMS_TO_TICKS(200)) == pdTRUE)
    {
      if (takeModem(2000))
      {
        if (item.kind == MQTT_PUB_KIND_EVENT)
          mqttPublishEventInternal(item.payload);
        else if (item.kind == MQTT_PUB_KIND_RESPONSE)
          mqttPublishResponseInternal(item.payload);
        else if (item.kind == MQTT_PUB_KIND_RESPONSE_TARGETED)
          mqttPublishResponseToInternal(item.topic, item.payload);
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
