#include "MqttHandler.h"
#include "WsCommandHandler.h" // To reuse command parsing logic if desired
#include "LteModem.h"

PubSubClient mqttClient(lteClient);

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("[MQTT] Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    // Convert payload to String for processing
    String message = "";
    for (int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.println(message);

    // You can route this directly to your existing WebSocket handler logic
    // or create a specific parse function here.
    // For now, let's assume we want to handle RESET or CONFIG via MQTT too.
    if (message == "RESET")
    {
        ESP.restart();
    }
}

bool mqttConnect()
{
    // NOTE: We do NOT lock the mutex here because PubSubClient.connect()
    // will internally call lteClient.connect() which needs the UART.
    // However, PubSubClient itself isn't aware of our mutex.
    // To be truly safe, we should only call this when we KNOW no other task is using the modem.

    if (mqttClient.connected())
        return true;

    Serial.print("[MQTT] Attempting connection...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
    {
        Serial.println("connected");
        mqttClient.subscribe(MQTT_TOPIC_COMMANDS);
        return true;
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        return false;
    }
}

void mqttPublishEvent(const char *payload)
{
    if (takeModem(2000))
    {
        if (!mqttClient.connected())
        {
            mqttConnect();
        }
        if (mqttClient.connected())
        {
            mqttClient.publish(MQTT_TOPIC_EVENTS, payload);
        }
        giveModem();
    }
}

void taskMqttLoop(void *)
{
    TickType_t wake = xTaskGetTickCount();
    while (true)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000)); // Check more frequently

        if (!lteGprsConnected)
        {
            // If LTE is down, ensure MQTT is disconnected to save resources
            if (mqttClient.connected())
            {
                if (takeModem(1000))
                {
                    Serial.println("[MQTT] LTE down, disconnecting MQTT");
                    mqttClient.disconnect();
                    giveModem();
                }
            }
            continue;
        }

        if (takeModem(2000))
        {
            if (!mqttClient.connected())
            {
                mqttConnect();
            }
            mqttClient.loop();
            giveModem();
        }
    }
}
