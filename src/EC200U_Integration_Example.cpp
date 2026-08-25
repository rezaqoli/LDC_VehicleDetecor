// ============================================================
// EC200U LTE Integration Example
// Integrating with Vehicle Detection Project (main.cpp)
// ============================================================

#include <Arduino.h>
#include "EC200U_LTE.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ========== Global Instance ==========
EC200U_LTE modem;

// ========== Configuration ==========
#define MQTT_SERVER "your-mqtt-broker.com"
#define MQTT_PORT 1883
#define APN "shatelmobile"          // Your SIM card APN
#define SIM_PIN ""              // Leave empty if no PIN

// Socket ID for data connection
static int32_t data_socket_id = -1;

// ========== Task for LTE Initialization ==========
void taskLTEInit(void *pvParameters)
{
  Serial.println("\n[LTE Task] Starting EC200U initialization...");

  // Initialize modem
  if (!modem.begin())
  {
    Serial.println("[LTE] Failed to initialize modem!");
    vTaskDelete(NULL);
    return;
  }

  // Check SIM card
  if (!modem.getSIMStatus())
  {
    Serial.println("[LTE] SIM card not ready or requires PIN");
    // Optionally enter PIN here:
    // modem.enterPIN(SIM_PIN);
    vTaskDelete(NULL);
    return;
  }

  // Wait for network registration (max 30 seconds)
  if (!modem.waitForRegistration(30000))
  {
    Serial.println("[LTE] Failed to register on network");
    vTaskDelete(NULL);
    return;
  }

  // Activate data connection
  if (!modem.activatePDP(APN))
  {
    Serial.println("[LTE] Failed to activate PDP");
    vTaskDelete(NULL);
    return;
  }

  Serial.println("[LTE] LTE connection ready!");

  // Now safe to use sockets
  vTaskDelete(NULL);
}

// ========== Task for Periodic Status Checking ==========
void taskLTEStatus(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10000); // 10 seconds

  while (1)
  {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    if (!modem.isInitialized())
      continue;

    // Get network info
    NetworkInfo net_info;
    if (modem.queryRegistrationStatus(net_info))
    {
      Serial.printf("[LTE Status] Network: %s, Registered: %s, Type: %s\n",
                    net_info.operator_name,
                    net_info.registered ? "YES" : "NO",
                    net_info.type == NetworkType::LTE_4G ? "4G" : "2G");

      Serial.printf("[LTE Status] Signal - RSSI: %d, RSRP: %d dBm, RSRQ: %d dB\n",
                    net_info.signal.rssi,
                    net_info.signal.rsrp,
                    net_info.signal.rsrq);
    }

    // Check PDP status
    if (modem.getPDPStatus())
    {
      Serial.println("[LTE Status] Data connection: ACTIVE");
    }
    else
    {
      Serial.println("[LTE Status] Data connection: INACTIVE");
    }
  }
}

// ========== MQTT Publishing Function ==========
// Simple MQTT packet encoder (for publish)
struct SimpleMQTTPacket
{
  static uint8_t *buildPublish(const char *topic, const uint8_t *payload, uint16_t payload_len,
                               uint16_t &packet_len)
  {
    // Allocate buffer for packet
    uint16_t topic_len = strlen(topic);
    packet_len = 1 + 1 + 2 + topic_len + payload_len; // fixed + var header + payload

    uint8_t *packet = new uint8_t[packet_len];
    if (!packet)
      return nullptr;

    uint16_t pos = 0;

    // Fixed header
    packet[pos++] = 0x30; // PUBLISH, QoS 0
    packet[pos++] = topic_len + 2 + payload_len; // remaining length

    // Variable header: topic
    packet[pos++] = (topic_len >> 8) & 0xFF;
    packet[pos++] = topic_len & 0xFF;
    memcpy(&packet[pos], topic, topic_len);
    pos += topic_len;

    // Payload
    memcpy(&packet[pos], payload, payload_len);
    pos += payload_len;

    return packet;
  }
};

// ========== Send Vehicle Detection Data via LTE ==========
bool sendVehicleDataOverLTE(const char *vehicle_type, float speed_kmh, float length_m)
{
  if (!modem.isInitialized() || !modem.getPDPStatus())
  {
    Serial.println("[Data] LTE not ready");
    return false;
  }

  // Ensure socket is open
  if (data_socket_id < 0)
  {
    // Open TCP socket to MQTT broker
    data_socket_id = modem.openSocket(MQTT_SERVER, MQTT_PORT, false);
    if (data_socket_id < 0)
    {
      Serial.println("[Data] Failed to open socket");
      return false;
    }
    Serial.printf("[Data] Socket opened: %ld\n", data_socket_id);
  }

  // Build MQTT CONNECT packet (simple version)
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"vehicle\":\"%s\",\"speed_kmh\":%.1f,\"length_m\":%.2f,\"timestamp\":%lu}",
           vehicle_type, speed_kmh, length_m, millis());

  // Send raw data (in production, use proper MQTT encoding)
  if (!modem.sendData(data_socket_id, (const uint8_t *)payload, strlen(payload)))
  {
    Serial.println("[Data] Failed to send data");
    modem.closeSocket(data_socket_id);
    data_socket_id = -1;
    return false;
  }

  Serial.printf("[Data] Sent: %s\n", payload);
  return true;
}

// ========== HTTP GET Request Example ==========
bool httpGetRequest(const char *host, uint16_t port, const char *path, char *response, size_t max_resp_len)
{
  if (!modem.isInitialized() || !modem.getPDPStatus())
  {
    Serial.println("[HTTP] LTE not ready");
    return false;
  }

  int32_t sock = modem.openSocket(host, port, false);
  if (sock < 0)
  {
    Serial.println("[HTTP] Failed to open socket");
    return false;
  }

  // Build HTTP request
  char request[512];
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
           path, host);

  // Send request
  if (!modem.sendData(sock, (const uint8_t *)request, strlen(request)))
  {
    Serial.println("[HTTP] Failed to send request");
    modem.closeSocket(sock);
    return false;
  }

  // Receive response
  uint16_t received = modem.receiveData(sock, (uint8_t *)response, max_resp_len - 1);
  if (received > 0)
  {
    response[received] = '\0';
  }

  modem.closeSocket(sock);
  return received > 0;
}

// ========== Firmware Update Over LTE ==========
bool downloadFirmwareUpdate(const char *url, const char *save_path)
{
  // This is a more complex operation - sketch for example
  // In real implementation, would stream data to SPIFFS/NVS

  if (!modem.isInitialized())
  {
    Serial.println("[FW Update] LTE not ready");
    return false;
  }

  // Parse URL to extract host and path
  // Open socket to server
  // Download file chunks
  // Save to flash
  // Trigger OTA update

  Serial.println("[FW Update] Not implemented in this example");
  return false;
}

// ========== AT Command Interactive Console ==========
// Useful for testing and debugging
void handleSerialATCommands()
{
  if (Serial.available())
  {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0)
      return;

    if (line.startsWith("AT"))
    {
      // Forward AT command to modem
      ATResponse resp = modem.sendCommand(line.c_str(), 5000);

      Serial.printf("Response (%d lines, %lu ms):\n",
                    resp.line_count, resp.response_time_ms);
      for (uint16_t i = 0; i < resp.line_count; i++)
      {
        Serial.println(resp.lines[i]);
      }
    }
    else if (line == "STATUS")
    {
      // Print modem status
      Serial.printf("Modem State: %s\n", modem.getStateString());
      Serial.printf("Initialized: %s\n", modem.isInitialized() ? "YES" : "NO");

      NetworkInfo info;
      if (modem.queryRegistrationStatus(info))
      {
        Serial.printf("Registered: %s\n", info.registered ? "YES" : "NO");
        Serial.printf("Operator: %s\n", info.operator_name);
        Serial.printf("Signal RSSI: %d\n", info.signal.rssi);
        Serial.printf("Signal RSRP: %d dBm\n", info.signal.rsrp);
      }
    }
    else if (line.startsWith("SEND "))
    {
      // Test send data: SEND <length> <data>
      // Example: SEND 5 hello
      String data = line.substring(5);
      if (data_socket_id >= 0)
      {
        modem.sendData(data_socket_id, (const uint8_t *)data.c_str(), data.length());
      }
      else
      {
        Serial.println("No socket open");
      }
    }
    else if (line.startsWith("CONNECT "))
    {
      // Test socket: CONNECT <host> <port>
      // Example: CONNECT google.com 80
      char host[30] = "google.com";
      unsigned short port = 80;
      int parts = sscanf(line.c_str(), "CONNECT %127s %hu", host, &port);
      if (parts == 2)
      {
        if (data_socket_id >= 0)
          modem.closeSocket(data_socket_id);

        data_socket_id = modem.openSocket(host, port, false);
        Serial.printf("Socket ID: %ld\n", data_socket_id);
      }
    }
    else if (line == "CLOSE")
    {
      if (data_socket_id >= 0)
      {
        modem.closeSocket(data_socket_id);
        data_socket_id = -1;
        Serial.println("Socket closed");
      }
    }
    else if (line == "HELP")
    {
      Serial.println("EC200U AT Commands:");
      Serial.println("  AT<cmd>           - Send raw AT command");
      Serial.println("  STATUS            - Show modem status");
      Serial.println("  CONNECT host port - Open socket");
      Serial.println("  SEND <data>       - Send data on open socket");
      Serial.println("  CLOSE             - Close socket");
      Serial.println("  HELP              - This message");
    }
  }
}

// ========== Integration with Main Setup ==========
// Add this to your main setup() function:
void setupLTE()
{
  Serial.println("\n[Setup] Initializing LTE...");

  // Create LTE initialization task
  if (xTaskCreatePinnedToCore(taskLTEInit, "LTE-Init", 8192, NULL, 1, NULL, 0) != pdPASS)
  {
    Serial.println("[Setup] Failed to create LTE init task");
  }

  // Create LTE status monitoring task
  if (xTaskCreatePinnedToCore(taskLTEStatus, "LTE-Status", 4096, NULL, 1, NULL, 1) != pdPASS)
  {
    Serial.println("[Setup] Failed to create LTE status task");
  }
}

// ========== Example Usage in Loop ==========
/*
In your main loop or tasks, you can now:

1. Send vehicle detection data:
   sendVehicleDataOverLTE("CAR", 65.5, 4.8);

2. Make HTTP requests:
   char response[1024];
   httpGetRequest("example.com", 80, "/api/status", response, sizeof(response));

3. Monitor network status in taskLTEStatus (runs every 10 seconds)

4. Use AT console for debugging

5. Implement full MQTT by building proper MQTT packets and
   managing connect/publish/disconnect flow
*/