#pragma once
#include "Config.h"

#define MQTT_MAX_PACKET_SIZE 4096
#define MQTT_KEEPALIVE 60

#include <PubSubClient.h>

extern PubSubClient mqttClient;

void mqttInit();
void taskMqttLoop(void *);
void mqttPublishEvent(const char* payload);
void mqttPublishResponse(const char* payload);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttCallback2(char *topic, byte *payload, unsigned int length);
bool mqttConnect();
// Locked variants — caller already holds modemMutex
bool mqttPublishEventLocked(const char *payload);
bool mqttPublishResponseLocked(const char *payload);