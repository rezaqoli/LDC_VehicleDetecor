#include "MqttHandler.h"
#include "WsCommandHandler.h"
#include "LteModem.h"
#include "WsUtils.h"

PubSubClient mqttClient(lteClient);

// -------------------------------------------------------
// Internal: assumes modemMutex already held
// -------------------------------------------------------
static bool mqttPublishLocked(const char *topic, const char *payload)
{
    if (!mqttClient.connected() || !topic || !payload)
        return false;

    size_t len = strlen(payload);
    if (len + 5 > MQTT_MAX_PACKET_SIZE) // 5 bytes MQTT overhead approx
    {
        Serial.printf("[MQTT] Publish SKIPPED to %s: payload %u > max %u\n",
                      topic, (unsigned)len, (unsigned)MQTT_MAX_PACKET_SIZE);
        return false;
    }

    bool ok = mqttClient.publish(topic, payload);
    if (!ok)
    {
        Serial.printf("[MQTT] Publish FAILED to %s (len=%u)\n",
                      topic, (unsigned)len);
    }
    return ok;
}

bool mqttPublishEventLocked(const char *payload)
{
    if (!payload) return false;
    bool ok = mqttPublishLocked(MQTT_TOPIC_EVENTS, payload);
    if (ok)
        Serial.printf("[MQTT] Published event (%u bytes)\n", (unsigned)strlen(payload));
    return ok;
}

bool mqttPublishResponseLocked(const char *payload)
{
    if (!payload) return false;
    bool ok = mqttPublishLocked(MQTT_TOPIC_COMMAND_RESPONSES, payload);
    if (ok)
        Serial.printf("[MQTT] Published response (%u bytes)\n", (unsigned)strlen(payload));
    return ok;
}

// -------------------------------------------------------

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

    if (message == "RESET")
    {
        ESP.restart();
    }
}

void mqttCallback2(char *topic, byte *payload, unsigned int length)
{
    String message = "";
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.printf("[MQTT] Command received: %s\n", message.c_str());

    // IMPORTANT: mqttCallback2 is invoked from mqttClient.loop() while
    // modemMutex is already held by taskMqttLoop.  Do NOT call
    // mqttPublishEvent() here (it would try to takeModem() again and
    // deadlock / fail). Use the Locked variant directly.
    auto reply = [](const char *msg)
    {
        // Locked: assumes mutex already held by taskMqttLoop
        mqttPublishResponseLocked(msg);
    };
    // processSystemCommand's third arg is WebSocket client id — pass 0 for MQTT
    processSystemCommand(message, reply, 0);
}

void mqttInit()
{
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback2);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    mqttClient.setSocketTimeout(60);
    // Fix #7: buffer must be set BEFORE connect, not after.
    // PubSubClient allocates buffer on heap; check return implicitly
    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    Serial.printf("[MQTT] Init: server=%s:%d keepalive=%d buf=%d\n",
                  MQTT_SERVER, MQTT_PORT, MQTT_KEEPALIVE, MQTT_MAX_PACKET_SIZE);
}

bool mqttConnect()
{
    if (mqttClient.connected())
        return true;

    // Ensure config is applied even if mqttInit() wasn't called (defensive)
    // setBufferSize before connect is required for large payloads.
    // It's cheap to call again; PubSubClient ignores if same size.
    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);

    Serial.println("\n[MQTT] ========================================");
    Serial.printf("[MQTT] Attempting connection to: %s:%d\n", MQTT_SERVER, MQTT_PORT);

    bool connected = false;
    // Fix: empty user/pass should use connect(id) overload, otherwise broker
    // may reject empty credentials with rc=4/5.
    if (MQTT_USER[0] == '\0' && MQTT_PASS[0] == '\0')
    {
        connected = mqttClient.connect(MQTT_CLIENT_ID);
    }
    else
    {
        connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
    }

    if (connected)
    {
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

void mqttPublishEvent(const char *payload)
{
    if (!payload) return;
    if (takeModem(2000))
    {
        if (!mqttClient.connected())
        {
            mqttConnect();
        }
        if (mqttClient.connected())
        {
            bool ok = mqttPublishLocked(MQTT_TOPIC_EVENTS, payload);
            if (ok)
                Serial.printf("[MQTT] Published event (%u bytes)\n", (unsigned)strlen(payload));
            else
                Serial.printf("[MQTT] FAILED event (%u bytes)\n", (unsigned)strlen(payload));
        }
        giveModem();
    }
    else
    {
        Serial.println("[MQTT] WARN: Could not lock modem for publish");
    }
}

// -------------------------------------------------------
// Public: publish command response (takes mutex)
// -------------------------------------------------------
void mqttPublishResponse(const char* payload)
{
    if (!payload || !lteGprsConnected) return;

    if (takeModem(3000))
    {
        if (!mqttClient.connected())
            mqttConnect();

        if (mqttClient.connected())
        {
            bool ok = mqttPublishLocked(MQTT_TOPIC_COMMAND_RESPONSES, payload);
            if (ok)
                Serial.printf("[MQTT] Published response (%u bytes)\n", (unsigned)strlen(payload));
        }
        giveModem();
    }
}


void taskMqttLoop(void *)
{
    TickType_t wake = xTaskGetTickCount();
    const TickType_t loopPeriod = pdMS_TO_TICKS(200); // call loop() every 200ms, not 10s
    uint32_t lastHeartbeatMs = 0;
    const uint32_t heartbeatIntervalMs = 60000; // 60s heartbeat, not every loop

    while (true)
    {
        vTaskDelayUntil(&wake, loopPeriod);

        if (!lteGprsConnected)
        {
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

        bool needWsHeartbeat = false;

        if (takeModem(500))
        {
            if (!mqttClient.connected())
            {
                mqttConnect();
            }
            else
            {
                mqttClient.loop();
            }

            // Heartbeat only periodically and only when connected
            uint32_t nowMs = millis();
            if (mqttClient.connected() && (nowMs - lastHeartbeatMs >= heartbeatIntervalMs))
            {
                // Use Locked variant — we already hold modemMutex,
                // so do NOT call mqttPublishEvent() which would deadlock.
                bool ok = mqttPublishEventLocked("[MQTT] Heartbeat: LTE is up and running");
                if (ok) lastHeartbeatMs = nowMs;
                // Defer wsSend until after mutex released
                needWsHeartbeat = true;
            }

            giveModem();
        }

        // wsSend does NOT need modemMutex; do it outside lock
        if (needWsHeartbeat)
        {
            wsSend("[MQTT] Heartbeat: LTE is up and running");
        }
    }
}
