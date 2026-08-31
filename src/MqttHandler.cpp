#include "MqttHandler.h"
#include "WsCommandHandler.h" // To reuse command parsing logic if desired
#include "LteModem.h"

PubSubClient mqttClient(lteClient);

static void mqttPublishEventInternal(const char *payload);
static void mqttPublishResponseInternal(const char *payload);

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

void mqttCallback2(char *topic, byte *payload, unsigned int length)
{
    String message = "";
    for (int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.printf("[MQTT] Command received: %s\n", message.c_str());

    auto reply = [](const char *msg)
    { mqttPublishEventInternal(msg); };
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
                      topic, payload ? strlen(payload) : 0);
    }
    return ok;
}


// bool mqttConnect()
// {
//     // NOTE: We do NOT lock the mutex here because PubSubClient.connect()
//     // will internally call lteClient.connect() which needs the UART.
//     // However, PubSubClient itself isn't aware of our mutex.
//     // To be truly safe, we should only call this when we KNOW no other task is using the modem.

//     if (mqttClient.connected())
//         return true;

//     Serial.print("[MQTT] Attempting connection...");
//     if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
//     {

//         Serial.println("connected");
//         mqttClient.subscribe(MQTT_TOPIC_COMMANDS);
//         return true;
//     }
//     else
//     {
//         Serial.print("failed, rc=");
//         Serial.print(mqttClient.state());
//         return false;
//     }
// }

// ------

bool mqttConnect()
{
    if (mqttClient.connected())
        return true;

    Serial.println("\n[MQTT] ========================================");
    Serial.printf("[MQTT] Attempting connection to: %s:%d\n", MQTT_SERVER, MQTT_PORT);

    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
    {
        mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE); // Increase buffer size if needed
        Serial.println("[MQTT] SUCCESS: Connected to MQTT Broker!");
        mqttClient.subscribe(MQTT_TOPIC_COMMANDS);
        Serial.printf("[MQTT] Subscribed to: %s\n", MQTT_TOPIC_COMMANDS);
        Serial.println("[MQTT] ========================================\n");
        return true;
    }
    else
    {
        int state = mqttClient.state();
        Serial.print("[MQTT] FAILED! State Code: ");
        Serial.print(state);

        // Translate the error code
        if (state < 0)
        {
            Serial.println(" (Network / DNS Layer Error)");
            if (state == -1)
                Serial.println("  -> TIMEOUT: DNS resolution likely failed or server unreachable.");
            else if (state == -2)
                Serial.println("  -> LOST: Connection lost during handshake.");
            else if (state == -3)
                Serial.println("  -> REFUSED: TCP connection failed. DNS might have resolved to wrong IP, or port is blocked.");
            else if (state == -4)
                Serial.println("  -> DISCONNECTED: Client disconnected.");
        }
        else
        {
            Serial.println(" (MQTT Protocol Layer Error)");
            if (state == 1)
                Serial.println("  -> Bad Protocol Version");
            else if (state == 2)
                Serial.println("  -> Bad Client ID");
            else if (state == 3)
                Serial.println("  -> Server Unavailable");
            else if (state == 4)
                Serial.println("  -> Bad Username/Password");
            else if (state == 5)
                Serial.println("  -> Unauthorized (ACL denied by broker)");
        }
        Serial.println("[MQTT] ========================================\n");
        return false;
    }
}

static void mqttPublishEventInternal(const char *payload)
{
    if (!mqttClient.connected())
    {
        mqttConnect();
    }
    if (mqttClient.connected())
    {
        bool ok = mqttPublishLocked(MQTT_TOPIC_EVENTS, payload);
        if (ok)
        {
            Serial.printf("[MQTT] Published event (%d bytes)\n", strlen(payload));
        }
        else
        {
            Serial.printf("[MQTT] FAILED event (%d bytes)\n", strlen(payload));
        }
    }
}

void mqttPublishEvent(const char *payload)
{
    if (takeModem(2000))
    {
        mqttPublishEventInternal(payload);
        giveModem();
    }
    else
    {
        Serial.println("[MQTT] WARN: Could not lock modem for publish");
    }
}


// -------------------------------------------------------
// Public: publish command response
// -------------------------------------------------------
static void mqttPublishResponseInternal(const char* payload)
{
    if (!payload || !lteGprsConnected) return;

    if (!mqttClient.connected())
        mqttConnect();

    if (mqttClient.connected())
    {
        bool ok = mqttPublishLocked(MQTT_TOPIC_COMMAND_RESPONSES, payload);
        if (ok)
            Serial.printf("[MQTT] Published response (%d bytes)\n", strlen(payload));
    }
}

void mqttPublishResponse(const char* payload)
{
    if (!payload || !lteGprsConnected) return;

    if (takeModem(3000))
    {
        mqttPublishResponseInternal(payload);
        giveModem();
    }
}


void taskMqttLoop(void *)
{
    TickType_t wake = xTaskGetTickCount();
    while (true)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10000)); // Check more frequently

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
            mqttPublishEventInternal("[MQTT] Heartbeat: LTE is up and running");
            wsSend("[MQTT] Heartbeat: LTE is up and running");
            mqttClient.loop();
            giveModem();
        }
    }
}
