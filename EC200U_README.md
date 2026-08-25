# Quectel EC200U LTE Module Driver for ESP32-S3

A comprehensive, production-ready AT command driver for the Quectel EC200U cellular module integrated with your ESP32-S3 vehicle detection system.

## Features

✅ **Core Functionality**
- Full AT command interface with timeout handling
- Automatic initialization and power management
- Network registration and signal monitoring
- Data connection (PDP context) management
- TCP/UDP socket operations
- Robust error handling and recovery

✅ **Vehicle Detection Integration**
- FreeRTOS task-based architecture
- Queue-based data communication
- Real-time network status monitoring
- Asynchronous operations (no blocking on network I/O)

✅ **Advanced Features**
- Unsolicited Response Handling (URCs)
- Signal quality monitoring (RSSI, RSRP, RSRQ, SINR)
- Low-power sleep modes
- Multi-socket support (up to 10 concurrent)
- Debug logging with selective verbosity

## Hardware Setup

### Wiring (EC200U to ESP32-S3)

```
EC200U Pin          ESP32-S3 Pin      Description
─────────────────────────────────────────────────
GND                 GND               Ground
VCC (4.3-4.8V)      +5V (via LDO)     Power supply
RX (UART)           GPIO1 (TX)        UART TX
TX (UART)           GPIO0 (RX)        UART RX
PWR_KEY             GPIO2             Power control
RST                 GPIO42            Reset control
DTR (optional)      GPIO41            Sleep mode
```

### Power Supply Requirements

- **Voltage**: 3.8V - 4.3V (typical 4.0V)
- **Current**: 
  - Idle: ~5mA
  - TX (full power): ~2A peak
  - Recommended PSU: 2A minimum

**Important**: The EC200U requires more current than ESP32 GPIO can supply. Use a dedicated power supply with bulk capacitance (1000µF+).

### Antenna

- Attach a compatible 2G/4G LTE antenna to the main antenna port
- For optimal signal, orient antenna vertically at 45° angle
- Indoor operation may require antenna near window

## Configuration

### APN Settings

Update the APN in your code based on your SIM card provider:

```cpp
#define APN "internet"  // Common for European carriers
// Or specific APN:
// "vodafoneinternet" (Vodafone)
// "web.vodafone.it" (Vodafone Italy)
// "o2internet" (O2)
// "telstra.internet" (Telstra Australia)
```

### Serial Port

By default, the driver uses UART1 (Serial1) with pins:
- RX: GPIO0
- TX: GPIO1

To change, modify in `EC200U_LTE.h`:
```cpp
#define EC200U_RX_PIN 0
#define EC200U_TX_PIN 1
#define EC200U_SERIAL_NUM 1  // UART0 or UART1
```

### Baud Rate

Default: 115200 bps (standard for EC200U)

**Note**: EC200U auto-bauds on first AT command. If not responding, try sending AT command at slower rate first.

## API Reference

### Initialization

```cpp
EC200U_LTE modem;

void setup() {
  // Initialize with defaults
  if (modem.begin()) {
    Serial.println("Modem initialized");
  }
}

void loop() {
  // Use modem...
  modem.atTest();  // Send AT test command
}
```

### Network Registration

```cpp
// Wait for network registration (blocking, max 30 seconds)
if (modem.waitForRegistration(30000)) {
  Serial.println("Connected to network");
}

// Check current registration status
NetworkInfo info;
if (modem.queryRegistrationStatus(info)) {
  printf("Operator: %s\n", info.operator_name);
  printf("RSSI: %d\n", info.signal.rssi);
  printf("RSRP: %d dBm\n", info.signal.rsrp);
}

// Get just signal quality
SignalQuality sig;
modem.getSignalQuality(sig);
printf("RSSI: %d, BER: %d\n", sig.rssi, sig.ber);
```

### Data Connection

```cpp
// Activate PDP context (data connection)
if (modem.activatePDP("internet")) {
  Serial.println("Data connection ready");
}

// Check if data is active
bool active = modem.getPDPStatus();

// Deactivate when done
modem.deactivatePDP();
```

### Socket Operations

```cpp
// Open TCP connection
int32_t sock = modem.openSocket("example.com", 80, false);
if (sock >= 0) {
  // Send data
  const char* data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
  modem.sendData(sock, (const uint8_t*)data, strlen(data));
  
  // Receive response
  uint8_t buffer[512];
  uint16_t len = modem.receiveData(sock, buffer, sizeof(buffer));
  
  // Close socket
  modem.closeSocket(sock);
}

// Open UDP connection
int32_t udp = modem.openSocket("8.8.8.8", 53, true);
```

### Device Information

```cpp
DeviceInfo info;
if (modem.getDeviceInfo(info)) {
  printf("Manufacturer: %s\n", info.manufacturer);
  printf("Model: %s\n", info.model);
  printf("Firmware: %s\n", info.fw_version);
  printf("IMEI: %s\n", info.imei);
  printf("IMSI: %s\n", info.imsi);
}
```

### Low-Level AT Commands

```cpp
// Send raw AT command
ATResponse resp = modem.sendCommand("AT+CMEE?", 5000);
if (resp.success) {
  for (uint16_t i = 0; i < resp.line_count; i++) {
    printf("Line %d: %s\n", i, resp.lines[i]);
  }
}

// Formatted command
ATResponse resp2 = modem.sendCommandFmt(5000, "AT+QIOPEN=1,0,\"TCP\",\"google.com\",%u", 80);
```

### Power Management

```cpp
// Sleep mode (reduced power consumption)
modem.sleep();
delay(5000);
modem.wakeup();

// Set power mode: 0=full, 1=sleep, 2=deep
modem.setPowerMode(1);
```

### SIM Card

```cpp
// Check SIM status
if (modem.getSIMStatus()) {
  Serial.println("SIM ready");
}

// Enter PIN if required
modem.enterPIN("1234");
```

## Integration with Vehicle Detection

### Example: Sending Detection Events

```cpp
// In your event handler:
void onVehicleDetected(const char* vehicle_type, float speed_kmh, float length_m) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"type\":\"%s\",\"speed\":%.1f,\"length\":%.2f,\"ts\":%lu}",
    vehicle_type, speed_kmh, length_m, millis());
  
  // Send over LTE
  if (modem.isInitialized() && modem.getPDPStatus()) {
    static int32_t sock = -1;
    
    if (sock < 0) {
      sock = modem.openSocket("api.example.com", 80, false);
    }
    
    if (sock >= 0) {
      modem.sendData(sock, (const uint8_t*)payload, strlen(payload));
    }
  }
}
```

### Example: FreeRTOS Integration

```cpp
// Create separate task for LTE communication
void taskLTEData(void *pvParameters) {
  // Initialize modem
  modem.begin();
  modem.waitForRegistration(30000);
  modem.activatePDP("internet");
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  while (1) {
    // Wait for event from detection queue
    EventData event;
    if (xQueueReceive(eventQueue, &event, pdMS_TO_TICKS(5000)) == pdTRUE) {
      // Send to server
      sendVehicleDataOverLTE(event.type, event.speed, event.length);
    }
    
    // Periodic status check
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10000));
    NetworkInfo info;
    modem.queryRegistrationStatus(info);
  }
}

// In main setup:
xTaskCreatePinnedToCore(taskLTEData, "LTE", 8192, NULL, 1, NULL, 1);
```

## AT Command Reference

### Most Useful Commands

| Command | Purpose | Example |
|---------|---------|---------|
| `AT` | Test communication | Test AT connectivity |
| `AT+CGSN` | Get IMEI | Device identification |
| `AT+CIMI` | Get IMSI | SIM identification |
| `AT+CREG?` | Check registration | Network registration status |
| `AT+COPS?` | Get operator | Current network operator |
| `AT+CSQ` | Signal quality | RSSI and BER |
| `AT+QCSQ` | LTE signal quality | RSRP, RSRQ, SINR for LTE |
| `AT+CGACT=1,1` | Activate PDP | Enable data connection |
| `AT+CGACT?` | Check PDP | Data connection status |
| `AT+QIOPEN` | Open socket | TCP/UDP connection |
| `AT+QISEND` | Send data | Send over socket |
| `AT+QIRECV` | Receive data | Read socket data |
| `AT+QICLOSE` | Close socket | Disconnect socket |
| `ATZ` | Soft reset | Reset modem |

### Error Codes

| Response | Meaning | Action |
|----------|---------|--------|
| `OK` | Command succeeded | - |
| `ERROR` | Command failed | Check command syntax |
| `+CME ERROR: 3` | Operation not allowed | Device may be initializing |
| `+CME ERROR: 30` | No network service | Check SIM/antenna |
| `+CME ERROR: 31` | Network timeout | Try again or move location |

## Troubleshooting

### Modem Not Responding

1. Check UART connections (RX/TX reversed?)
2. Verify power supply (need 4.0V minimum)
3. Try slower baud rate first (might need reset)
4. Hard reset: `modem.hardReset()`
5. Enable debug: `modem.enableDebug(true)`

### No Network Registration

1. Check antenna connection
2. Verify SIM card is activated
3. Try different network bands: `modem.setNetworkMode(NetworkType::LTE_4G)`
4. Check signal: `modem.queryRegistrationStatus(info)`
5. Wait longer (30-60 seconds): `modem.waitForRegistration(60000)`

### Poor Signal Quality

1. Reorient antenna (45° vertical)
2. Move closer to window
3. Check for metal obstructions
4. RSSI > 10 is acceptable, > 20 is good

### Socket Connection Fails

1. Verify data connection: `modem.getPDPStatus()`
2. Check server availability
3. Verify hostname resolution (try IP address)
4. Check firewall rules
5. Monitor signal quality during connection attempt

### Memory Issues

- Default buffers: 2KB RX, 512B TX
- Adjust in `EC200U_LTE.h` if needed
- Monitor with: `Serial.printf("Free heap: %u\n", esp_get_free_heap_size());`

## Performance Considerations

### Latency
- Network registration: 5-30 seconds (first time)
- Socket open: 500-2000ms
- Data send: 10-100ms per packet
- RSSI query: 100-500ms

### Power Consumption
- Idle (registered): ~5mA
- Transmitting: 1.5-2A peak
- Sleep mode: <2mA
- Deep sleep: <1mA

### Socket Limits
- Maximum concurrent sockets: 10
- Maximum packet size: ~1500 bytes
- Recommended: 1-3 concurrent sockets for embedded systems

## Real-World Example: Complete Setup

```cpp
#include "EC200U_LTE.h"

EC200U_LTE modem;

void setupLTE() {
  // Initialize with power control
  if (!modem.begin()) {
    Serial.println("Failed to initialize");
    return;
  }
  
  // Wait for network
  if (!modem.waitForRegistration(30000)) {
    Serial.println("Not registered");
    return;
  }
  
  // Activate data
  if (!modem.activatePDP("internet")) {
    Serial.println("Failed to activate PDP");
    return;
  }
  
  Serial.println("LTE Ready!");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  setupLTE();
}

void loop() {
  // Example: send data every 10 seconds
  static unsigned long lastSend = 0;
  
  if (millis() - lastSend > 10000) {
    lastSend = millis();
    
    // Open connection
    int32_t sock = modem.openSocket("api.example.com", 80, false);
    if (sock >= 0) {
      const char* msg = "Temperature: 25.5C";
      modem.sendData(sock, (const uint8_t*)msg, strlen(msg));
      modem.closeSocket(sock);
      Serial.println("Data sent");
    }
  }
}
```

## Testing

### AT Command Console

The integration example includes an AT console for testing:

```
Serial Input → AT command processing → Response printing

Commands:
  AT<cmd>           Send raw AT command
  STATUS            Show modem status
  CONNECT host port Open socket
  SEND <data>       Send data
  CLOSE             Close socket
  HELP              Help message
```

### Common Test Sequence

```
1. AT                    → OK
2. AT+CPIN?              → +CPIN: READY
3. AT+CREG?              → +CREG: 0,1 (registered)
4. AT+CGACT=1,1          → OK (activate data)
5. AT+CGACT?             → +CGACT: 1,1 (active)
6. AT+QIOPEN=1,0,"TCP","8.8.8.8",53 → +QIOPEN: 0,0
```

## Support and Resources

- **Quectel EC200U Datasheet**: Available from Quectel
- **AT Command Manual**: Comprehensive command reference
- **Signal Quality Guidelines**:
  - RSSI: -120 to -25 dBm (higher is better)
  - RSRP: -140 to -44 dBm (higher is better)
  - RSRQ: -20 to -3 dB (higher is better)
  - SINR: -20 to 30 dB (higher is better)

## License and Credits

Developed for ESP32-S3 vehicle detection system integration.
Compatible with Arduino IDE and PlatformIO.

---

**Last Updated**: 2025
**Status**: Production Ready