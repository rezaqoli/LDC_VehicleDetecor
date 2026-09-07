#pragma once
#include "Config.h"

#define MQTT_ENABLE 1
#define MQTT_MAX_PACKET_SIZE 4096
#define MQTT_KEEPALIVE 60
#define MQTT_PUB_PAYLOAD_MAX 512

#include <PubSubClient.h>

extern PubSubClient mqttClient;

void mqttInit();
void taskMqttLoop(void *);
void taskMqttPublisher(void *);
void mqttPublishEvent(const char* payload);
void mqttPublishResponse(const char* payload);
void mqttPublishResponseTo(const char *topic, const char *payload);
void mqttPublishCallback(char* topic, byte* payload, unsigned int length);
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool mqttConnect();
bool mqttHasClientId();

// Used by MqttReplyChannel to prefix RSP|<cmd_id>| on outgoing replies.
const char *mqttLastCmdId();
void mqttClearLastCmdId();

// Active per-board topic strings (rebuilt every connect from mqttClientId).
const char *mqttTopicEventsBoard();
const char *mqttTopicCommandsBoard();
const char *mqttTopicResponsesBoard();

// Diagnostics counters (atomic-ish for single-writer/single-reader)
extern volatile uint32_t mqttPubPublished;     // sum of evt + rsp (back-compat)
extern volatile uint32_t mqttPubPublishedEvt; // events topic
extern volatile uint32_t mqttPubPublishedRsp; // command_responses topic
extern volatile uint32_t mqttPubDropped;
extern volatile uint32_t mqttPubReconnect;
extern char             mqttPubLastErr[64];

void setLastErr(const char *s);