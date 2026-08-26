#pragma once
#include "Config.h"
#include <PubSubClient.h>

extern PubSubClient mqttClient;

void taskMqttLoop(void *);
void mqttPublishEvent(const char* payload);
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool mqttConnect();