#pragma once
#include "Config.h"

#define MQTT_ENABLE 1
#define MQTT_MAX_PACKET_SIZE 4096
#define MQTT_KEEPALIVE 60

#include <PubSubClient.h>

extern PubSubClient mqttClient;

void mqttInit();
void taskMqttLoop(void *);
void taskMqttPublisher(void *);
void mqttPublishEvent(const char* payload);
void mqttPublishCallback(char* topic, byte* payload, unsigned int length);
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool mqttConnect();

// Diagnostics counters (atomic-ish for single-writer/single-reader)
extern volatile uint32_t mqttPubPublished;
extern volatile uint32_t mqttPubDropped;
extern volatile uint32_t mqttPubReconnect;
extern char             mqttPubLastErr[64];

void setLastErr(const char *s);