# EC200U Practical Examples for Vehicle Detection

Real-world code examples for integrating the EC200U LTE module with your ESP32-S3 vehicle detection system.

## Example 1: Basic Initialization

```cpp
#include "EC200U_LTE.h"

EC200U_LTE modem;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("Starting EC200U initialization...");
  
  // Initialize modem
  if (!modem.begin()) {
    Serial.println("FATAL: Modem initialization failed!");
    while(1);
  }
  
  Serial.println("Modem initialized successfully");
  Serial.printf("State: %s\n", modem.getStateString());
}

void loop() {
  // Modem is ready to use
  delay(1000);
}
```

---

## Example 2: Network Registration and Signal Monitoring

```cpp
void monitorNetworkStatus() {
  // Get current network info
  NetworkInfo info;
  
  if (!modem.queryRegistrationStatus(info)) {
    Serial.println("Failed to query network status");
    return;
  }
  
  // Display results
  Serial.printf("=== Network Status ===\n");
  Serial.printf("Registered: %s\n", info.registered ? "YES" : "NO");
  Serial.printf("Roaming: %s\n", info.roaming ? "YES" : "NO");
  Serial.printf("Operator: %s\n", info.operator_name);
  Serial.printf("Network Type: %s\n", 
    info.type == NetworkType::LTE_4G ? "LTE 4G" : 
    info.type == NetworkType::GSM_2G ? "GSM 2G" : "Unknown");
  
  Serial.printf("\n=== Signal Quality ===\n");
  Serial.printf("RSSI: %d (-120 to -25 dBm)\n", 
    (int)(-120 + info.signal.rssi * 2));
  Serial.printf("BER: %d%%\n", info.signal.ber * 100 / 8);
  
  if (info.type == NetworkType::LTE_4G) {
    Serial.printf("RSRP: %d dBm\n", info.signal.rsrp);
    Serial.printf("RSRQ: %d dB\n", info.signal.rsrq);
    Serial.printf("SINR: %d dB\n", info.signal.sinr);
  }
}

// In setup():
void setup() {
  modem.begin();
  
  // Wait for network registration
  Serial.println("Waiting for network registration...");
  if (!modem.waitForRegistration(30000)) {
    Serial.println("ERROR: Network registration timeout!");
    return;
  }
  
  Serial.println("Successfully registered on network");
  monitorNetworkStatus();
}
```

---

## Example 3: Simple HTTP GET Request

```cpp
bool httpGet(const char *host, const char *path, char *response, size_t max_len) {
  // Ensure data connection is active
  if (!modem.getPDPStatus()) {
    Serial.println("ERROR: PDP context not active");
    return false;
  }
  
  // Open TCP socket
  int32_t sock = modem.openSocket(host, 80, false);
  if (sock < 0) {
    Serial.printf("ERROR: Failed to open socket to %s\n", host);
    return false;
  }
  
  Serial.printf("Socket opened: %ld\n", sock);
  
  // Build HTTP request
  char request[512];
  snprintf(request, sizeof(request),
    "GET %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "\r\n",
    path, host);
  
  // Send request
  if (!modem.sendData(sock, (const uint8_t*)request, strlen(request))) {
    Serial.println("ERROR: Failed to send request");
    modem.closeSocket(sock);
    return false;
  }
  
  Serial.println("Request sent, waiting for response...");
  delay(1000);  // Give server time to respond
  
  // Receive response
  uint16_t received = modem.receiveData(sock, (uint8_t*)response, max_len - 1);
  if (received > 0) {
    response[received] = '\0';
    Serial.printf("Received %d bytes\n", received);
  } else {
    Serial.println("No response received");
  }
  
  modem.closeSocket(sock);
  return received > 0;
}

// Usage:
void setup() {
  modem.begin();
  modem.waitForRegistration(30000);
  modem.activatePDP("internet");
  
  char response[1024];
  if (httpGet("example.com", "/api/status", response, sizeof(response))) {
    Serial.println("Response received:");
    Serial.println(response);
  }
}
```

---

## Example 4: Vehicle Detection Data Upload

```cpp
struct VehicleEvent {
  char type[32];        // "CAR", "TRUCK", etc.
  float speed_kmh;
  float length_m;
  uint32_t timestamp;
};

bool sendVehicleEvent(const VehicleEvent &event) {
  // Ensure connectivity
  if (!modem.isInitialized()) {
    Serial.println("ERROR: Modem not initialized");
    return false;
  }
  
  if (!modem.getPDPStatus()) {
    Serial.println("ERROR: Data connection not active");
    return false;
  }
  
  // Build JSON payload
  char payload[256];
  int len = snprintf(payload, sizeof(payload),
    "{"
    "\"vehicle_type\":\"%s\","
    "\"speed_kmh\":%.1f,"
    "\"length_m\":%.2f,"
    "\"timestamp\":%lu"
    "}",
    event.type, event.speed_kmh, event.length_m, event.timestamp);
  
  if (len <= 0 || len >= (int)sizeof(payload)) {
    Serial.println("ERROR: Payload too large");
    return false;
  }
  
  // Open connection to server
  static int32_t sock = -1;
  
  if (sock < 0) {
    sock = modem.openSocket("api.yourdomain.com", 80, false);
    if (sock < 0) {
      Serial.println("ERROR: Failed to open socket");
      return false;
    }
  }
  
  // Build HTTP POST request
  char request[512];
  snprintf(request, sizeof(request),
    "POST /api/vehicles HTTP/1.1\r\n"
    "Host: api.yourdomain.com\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "%s",
    len, payload);
  
  // Send
  if (!modem.sendData(sock, (const uint8_t*)request, strlen(request))) {
    Serial.println("ERROR: Failed to send vehicle data");
    modem.closeSocket(sock);
    sock = -1;
    return false;
  }
  
  Serial.printf("Sent: %s\n", payload);
  return true;
}

// Integration with detector callback:
void onVehicleDetected(const char *type, float speed, float length) {
  VehicleEvent event = {
    .speed_kmh = speed,
    .length_m = length,
    .timestamp = millis()
  };
  strncpy(event.type, type, sizeof(event.type) - 1);
  
  if (sendVehicleEvent(event)) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }
}
```

---

## Example 5: FreeRTOS Task-Based Integration

```cpp
// Global modem instance
EC200U_LTE modem;
QueueHandle_t vehicleEventQueue;

// Task 1: Modem initialization and network management
void taskModemInit(void *pvParameters) {
  Serial.println("[Modem] Task started");
  
  // Initialize
  if (!modem.begin()) {
    Serial.println("[Modem] FATAL: Initialization failed");
    vTaskDelete(NULL);
    return;
  }
  
  // Wait for network
  if (!modem.waitForRegistration(60000)) {
    Serial.println("[Modem] ERROR: Network registration failed");
    vTaskDelete(NULL);
    return;
  }
  
  // Activate data connection
  if (!modem.activatePDP("internet")) {
    Serial.println("[Modem] ERROR: PDP activation failed");
    vTaskDelete(NULL);
    return;
  }
  
  Serial.println("[Modem] Ready!");
  vTaskDelete(NULL);
}

// Task 2: Periodic network monitoring
void taskNetworkMonitor(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(15000);  // 15 seconds
  
  while (1) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    
    if (!modem.isInitialized())
      continue;
    
    NetworkInfo info;
    if (modem.queryRegistrationStatus(info)) {
      Serial.printf("[Network] %s, RSSI: %d, RSRP: %d dBm\n",
        info.operator_name,
        (int)(-120 + info.signal.rssi * 2),
        info.signal.rsrp);
    }
  }
}

// Task 3: Vehicle event uploader
void taskVehicleUploader(void *pvParameters) {
  VehicleEvent event;
  
  while (1) {
    // Wait for event from detection system
    if (xQueueReceive(vehicleEventQueue, &event, pdMS_TO_TICKS(5000)) == pdTRUE) {
      // Send to server
      sendVehicleEvent(event);
    }
  }
}

// Setup function
void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Create event queue
  vehicleEventQueue = xQueueCreate(10, sizeof(VehicleEvent));
  
  // Create tasks
  xTaskCreatePinnedToCore(taskModemInit, "ModemInit", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskNetworkMonitor, "NetMon", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskVehicleUploader, "Uploader", 8192, NULL, 1, NULL, 1);
  
  // Your other initialization...
}

// Send event from detector
void onVehicleDetected(const char *type, float speed, float length) {
  if (!vehicleEventQueue)
    return;
  
  VehicleEvent event = {
    .speed_kmh = speed,
    .length_m = length,
    .timestamp = millis()
  };
  strncpy(event.type, type, sizeof(event.type) - 1);
  
  xQueueSendToBackFromISR(vehicleEventQueue, &event, NULL);
}
```

---

## Example 6: Error Recovery and Reconnection

```cpp
class ModemConnectionManager {
private:
  EC200U_LTE &modem;
  uint32_t last_check_time;
  bool is_connected;
  
public:
  ModemConnectionManager(EC200U_LTE &m) : modem(m), last_check_time(0), is_connected(false) {}
  
  bool ensureConnected(uint32_t check_interval = 30000) {
    uint32_t now = millis();
    
    // Check periodically, not on every call
    if (now - last_check_time < check_interval) {
      return is_connected;
    }
    
    last_check_time = now;
    
    // Check if modem is responsive
    if (!modem.atTest()) {
      Serial.println("[ConnMgr] Modem not responding, attempting reset...");
      if (modem.hardReset()) {
        if (modem.waitForRegistration(30000)) {
          if (modem.activatePDP("internet")) {
            is_connected = true;
            return true;
          }
        }
      }
      is_connected = false;
      return false;
    }
    
    // Check network registration
    NetworkInfo info;
    if (!modem.queryRegistrationStatus(info)) {
      Serial.println("[ConnMgr] Registration query failed");
      is_connected = false;
      return false;
    }
    
    if (!info.registered) {
      Serial.println("[ConnMgr] Not registered, re-registering...");
      if (!modem.waitForRegistration(30000)) {
        is_connected = false;
        return false;
      }
    }
    
    // Check data connection
    if (!modem.getPDPStatus()) {
      Serial.println("[ConnMgr] PDP inactive, reactivating...");
      if (!modem.activatePDP("internet")) {
        is_connected = false;
        return false;
      }
    }
    
    is_connected = true;
    return true;
  }
  
  bool isConnected() const {
    return is_connected;
  }
};

// Usage:
ModemConnectionManager connMgr(modem);

void taskWithReconnect(void *pvParameters) {
  while (1) {
    if (!connMgr.ensureConnected()) {
      Serial.println("Connection lost, retrying...");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    
    // Do your work here
    sendVehicleEvent(event);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
```

---

## Example 7: AT Command Console for Debugging

```cpp
void handleSerialInput() {
  if (!Serial.available())
    return;
  
  String line = Serial.readStringUntil('\n');
  line.trim();
  
  if (line.length() == 0)
    return;
  
  if (line.startsWith("AT")) {
    // Forward to modem
    ATResponse resp = modem.sendCommand(line.c_str(), 5000);
    
    Serial.printf("\n[Response %d lines, %lu ms]\n", 
      resp.line_count, resp.response_time_ms);
    
    for (uint16_t i = 0; i < resp.line_count; i++) {
      Serial.println(resp.lines[i]);
    }
    Serial.println();
  }
  else if (line == "STATUS") {
    Serial.printf("Modem State: %s\n", modem.getStateString());
    Serial.printf("Initialized: %s\n", modem.isInitialized() ? "YES" : "NO");
    
    NetworkInfo info;
    if (modem.queryRegistrationStatus(info)) {
      Serial.printf("Network: %s\n", info.operator_name);
      Serial.printf("Registered: %s\n", info.registered ? "YES" : "NO");
      Serial.printf("RSSI: %d\n", (int)(-120 + info.signal.rssi * 2));
      Serial.printf("Data: %s\n", modem.getPDPStatus() ? "ACTIVE" : "INACTIVE");
    }
  }
  else if (line == "HELP") {
    Serial.println("\nAT Commands:");
    Serial.println("  AT<cmd>     - Send AT command");
    Serial.println("  STATUS      - Show modem status");
    Serial.println("  HELP        - This message\n");
  }
}

void loop() {
  handleSerialInput();
  // ... rest of loop code
}
```

---

## Example 8: MQTT-Style Message Publishing

```cpp
// Simple MQTT CONNECT packet builder
bool mqttConnect(int32_t sock, const char *client_id) {
  // MQTT CONNECT packet format
  uint8_t packet[64] = {
    0x10,                    // CONNECT command
    0x00,                    // Will be updated with length
    0x00, 0x04, 'M', 'Q', 'T', 'T',  // Protocol name
    0x04,                    // Protocol level 4
    0x02,                    // Connect flags (clean session)
    0x00, 0x3C,              // Keep alive 60s
    0x00, 0x00,              // Client ID length (will be updated)
  };
  
  // Add client ID
  size_t client_len = strlen(client_id);
  if (client_len > 255) client_len = 255;
  
  size_t pos = 12;
  packet[pos++] = (client_len >> 8) & 0xFF;
  packet[pos++] = client_len & 0xFF;
  memcpy(&packet[pos], client_id, client_len);
  pos += client_len;
  
  // Update packet length
  packet[1] = pos - 2;
  
  return modem.sendData(sock, packet, pos);
}

// Simplified MQTT PUBLISH packet builder
bool mqttPublish(int32_t sock, const char *topic, const char *message) {
  size_t topic_len = strlen(topic);
  size_t msg_len = strlen(message);
  
  // Allocate packet buffer
  size_t packet_size = 1 + 1 + 2 + topic_len + msg_len;
  uint8_t *packet = new uint8_t[packet_size];
  if (!packet) return false;
  
  size_t pos = 0;
  
  // Fixed header
  packet[pos++] = 0x30;  // PUBLISH, QoS 0
  packet[pos++] = topic_len + 2 + msg_len;  // Remaining length
  
  // Variable header: topic
  packet[pos++] = (topic_len >> 8) & 0xFF;
  packet[pos++] = topic_len & 0xFF;
  memcpy(&packet[pos], topic, topic_len);
  pos += topic_len;
  
  // Payload
  memcpy(&packet[pos], message, msg_len);
  pos += msg_len;
  
  bool result = modem.sendData(sock, packet, pos);
  delete[] packet;
  
  return result;
}

// Usage:
void mqttExample() {
  // Connect to MQTT broker
  int32_t sock = modem.openSocket("mqtt.broker.com", 1883, false);
  if (sock < 0) return;
  
  // Send CONNECT
  mqttConnect(sock, "esp32-vehicle-detector");
  
  // Send PUBLISH
  mqttPublish(sock, "vehicles/detection", "{\"type\":\"CAR\",\"speed\":65}");
  
  modem.closeSocket(sock);
}
```

---

## Tips & Tricks

### 1. Keep Sockets Open
```cpp
// GOOD: Reuse socket
static int32_t sock = -1;
if (sock < 0) {
  sock = modem.openSocket("api.example.com", 80, false);
}
modem.sendData(sock, data, len);  // Reuse socket

// AVOID: Opening new socket every time
int32_t sock = modem.openSocket(...);
modem.sendData(sock, ...);
modem.closeSocket(sock);
// Inefficient!
```

### 2. Batch Updates
```cpp
// Instead of sending one event at a time
char batch[512];
int pos = snprintf(batch, sizeof(batch), "[");
for (int i = 0; i < events.size(); i++) {
  pos += snprintf(batch + pos, sizeof(batch) - pos, 
    "%s{\"type\":\"%s\",\"speed\":%.1f}",
    i > 0 ? "," : "", events[i].type, events[i].speed);
}
pos += snprintf(batch + pos, sizeof(batch) - pos, "]");
modem.sendData(sock, (uint8_t*)batch, pos);
```

### 3. Signal Quality Monitoring
```cpp
// Monitor signal before transmitting
SignalQuality sig;
modem.getSignalQuality(sig);

if (sig.rssi < 10) {
  Serial.println("Warning: Weak signal, may fail");
  // Could delay transmission or increase timeout
}
```

### 4. Graceful Degradation
```cpp
// Try to send, but don't block forever
bool sendWithTimeout(int32_t sock, const uint8_t *data, uint16_t len) {
  if (!modem.isInitialized()) {
    Serial.println("Modem offline, skipping");
    return false;
  }
  
  if (!modem.getPDPStatus()) {
    Serial.println("No data connection, skipping");
    return false;
  }
  
  if (!modem.sendData(sock, data, len)) {
    Serial.println("Send failed, will retry");
    return false;
  }
  
  return true;
}
```

---

**Happy coding!** These examples should cover most common use cases for vehicle detection data transmission over LTE.