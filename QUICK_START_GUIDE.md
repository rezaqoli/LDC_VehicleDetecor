# EC200U LTE Module - Quick Start Guide

**5-minute setup for vehicle detection project**

## Installation

1. **Copy files to your project:**
   ```
   project/
   ├── EC200U_LTE.h
   ├── EC200U_LTE.cpp
   ├── main.cpp (your existing file)
   └── platformio.ini
   ```

2. **Update `platformio.ini`:**
   ```ini
   [env:esp32-s3-devkitc-1]
   platform = espressif32
   board = esp32-s3-devkitc-1
   framework = arduino
   monitor_speed = 115200
   build_flags = -std=c++17
   ```

3. **Add to your `main.cpp`:**
   ```cpp
   #include "EC200U_LTE.h"
   
   EC200U_LTE modem;
   
   void setupLTE() {
     Serial.println("Initializing LTE...");
     
     if (!modem.begin()) {
       Serial.println("Failed!");
       return;
     }
     
     if (!modem.waitForRegistration(30000)) {
       Serial.println("No network!");
       return;
     }
     
     if (!modem.activatePDP("internet")) {
       Serial.println("PDP failed!");
       return;
     }
     
     Serial.println("LTE Ready!");
   }
   
   void setup() {
     // ... your existing setup ...
     setupLTE();  // Add this
   }
   ```

## Hardware Wiring (5 minutes)

| EC200U | ESP32-S3 | Notes |
|--------|----------|-------|
| GND | GND | Ground |
| VCC | +4V via LDO | Use 2A PSU with bulk cap |
| RX | GPIO1 (TX) | UART1 data |
| TX | GPIO0 (RX) | UART1 data |
| PWR_KEY | GPIO2 | Power control |
| RST | GPIO42 | Reset (optional) |

**Critical**: EC200U needs **separate power supply** — ESP32 GPIO can't supply 2A peaks.

## First Test (2 minutes)

Open Serial Monitor (115200 baud) and watch:

```
[LTE Task] Starting EC200U initialization...
[EC200U] Initializing...
[EC200U] Powering on...
[EC200U TX] AT
[EC200U RX] OK
[EC200U] Device booted in 1500 ms
[EC200U] Initialized successfully
Waiting for network registration...
[LTE Status] Network: Vodafone, Registered: YES, Type: 4G
Successfully registered on network
```

If you get no response:
1. Check power: should read 4.0-4.2V on EC200U VCC pin
2. Check UART wiring: TX/RX pins correct and not reversed
3. Check antenna: connected to EC200U main antenna port

## Send First Data (10 minutes)

```cpp
void sendTestData() {
  if (!modem.isInitialized() || !modem.getPDPStatus()) {
    Serial.println("Not ready");
    return;
  }
  
  // Open socket
  int32_t sock = modem.openSocket("api.example.com", 80, false);
  if (sock < 0) {
    Serial.println("Socket failed");
    return;
  }
  
  // Send data
  const char* data = "GET / HTTP/1.1\r\nHost: api.example.com\r\n\r\n";
  modem.sendData(sock, (const uint8_t*)data, strlen(data));
  
  // Close
  modem.closeSocket(sock);
  Serial.println("Data sent!");
}

// In your main loop or task:
sendTestData();
```

## Common Issues & Solutions

### "No response to: AT"
- **Problem**: UART not communicating
- **Check**: 
  - Power: LED on EC200U should blink slowly
  - Wiring: TX/RX not reversed
  - Baud: Should be 115200
- **Fix**: Try `modem.hardReset()`

### "Registration timeout"
- **Problem**: Modem not finding network
- **Check**:
  - Antenna attached and oriented vertically
  - SIM card activated
  - APN correct for your carrier
- **Fix**: Move closer to window, try different location

### "PDP activation failed"
- **Problem**: No data connection
- **Check**:
  - SIM has data plan active
  - Network signal strong (RSSI > 10)
- **Fix**: 
  ```cpp
  modem.enableDebug(true);  // Show all AT commands
  modem.activatePDP("internet");
  ```

### "Modem not responding" (intermittent)
- **Problem**: Weak power supply
- **Check**: Power supply can deliver 2A sustained
- **Fix**: Add larger capacitor (1000µF) near EC200U VCC

## Integration with Vehicle Detection

### Add to your detector callback:

```cpp
void onVehicleDetected(const char *type, float speed, float length) {
  // Existing code...
  
  // Send over LTE
  char msg[256];
  snprintf(msg, sizeof(msg),
    "{\"vehicle\":\"%s\",\"speed\":%.1f,\"length\":%.2f}",
    type, speed, length);
  
  static int32_t sock = -1;
  if (sock < 0) {
    sock = modem.openSocket("your-api.com", 80, false);
  }
  
  if (sock >= 0) {
    modem.sendData(sock, (const uint8_t*)msg, strlen(msg));
  }
}
```

### Monitor signal in status task:

```cpp
void taskNetworkStatus(void *pvParameters) {
  while (1) {
    SignalQuality sig;
    if (modem.getSignalQuality(sig)) {
      Serial.printf("RSSI: %d, RSRP: %d dBm\n",
        sig.rssi, sig.rsrp);
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
```

## API Quick Reference

### Initialization
```cpp
modem.begin()                           // Initialize
modem.end()                             // Shutdown
modem.isInitialized()                   // Check state
modem.getStateString()                  // Get state name
```

### Network
```cpp
modem.waitForRegistration(timeout_ms)   // Wait for network
modem.queryRegistrationStatus(info)     // Get current status
modem.getSignalQuality(quality)         // Get RSSI/RSRP/RSRQ
```

### Data Connection
```cpp
modem.activatePDP("apn_name")          // Activate data
modem.getPDPStatus()                   // Check if active
modem.deactivatePDP()                  // Deactivate
```

### Sockets (TCP/UDP)
```cpp
int32_t sock = modem.openSocket(host, port, is_udp);
modem.sendData(sock, data, length);
uint16_t len = modem.receiveData(sock, buffer, max_len);
modem.closeSocket(sock);
```

### Low-Level AT
```cpp
ATResponse resp = modem.sendCommand("AT+CSQ");
if (resp.success) {
  for (int i = 0; i < resp.line_count; i++) {
    Serial.println(resp.lines[i]);
  }
}
```

## Performance Expectations

| Operation | Time | Notes |
|-----------|------|-------|
| Initialization | 5-15s | First boot, waits for network |
| Network registration | 10-30s | Depends on signal |
| PDP activation | 1-5s | After registration |
| Socket open | 0.5-2s | TCP/UDP connect |
| Data send (100 bytes) | 10-100ms | Depends on signal |
| RSSI query | 100-500ms | Signal check |

## Testing with AT Console

Modem supports raw AT commands. Add to your main loop:

```cpp
void handleATConsole() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("AT")) {
      ATResponse resp = modem.sendCommand(cmd.c_str(), 5000);
      Serial.printf("[%s]\n", resp.success ? "OK" : "FAIL");
      for (uint16_t i = 0; i < resp.line_count; i++) {
        Serial.println(resp.lines[i]);
      }
    }
  }
}
```

**Useful test commands:**
```
AT                  → OK (connection test)
AT+CREG?            → +CREG: 0,1 (registered)
AT+COPS?            → +COPS: ... (operator name)
AT+CSQ              → +CSQ: rssi,ber (signal quality)
AT+CGACT?           → +CGACT: 1,1 (data connection active)
```

## Next Steps

1. **Read the full documentation**: `EC200U_README.md`
2. **Explore AT commands**: `AT_COMMAND_CHEATSHEET.md`
3. **Try practical examples**: `PRACTICAL_EXAMPLES.md`
4. **Integrate with your system**: Copy integration patterns from examples

## Support

- **Check logs**: Enable `modem.enableDebug(true)` for detailed output
- **Verify hardware**: Confirm 4.0-4.2V on EC200U VCC pin
- **Test manually**: Use AT console to send commands directly
- **Monitor signal**: Watch RSSI in status output, should be > 10

## Carrier APNs

| Carrier | Country | APN |
|---------|---------|-----|
| Vodafone | IT | web.vodafone.it |
| Vodafone | DE | vodafoneinternet |
| O2 | UK | o2internet |
| Telstra | AU | telstra.internet |
| Orange | FR | orange.fr |
| Generic | Any | internet |

## Troubleshooting Flowchart

```
No response?
  ├─ No LED on EC200U → Check power (4.0-4.2V)
  ├─ LED blinks → Check TX/RX wiring
  └─ LED steady → Try hard reset

Registers but no data?
  ├─ Weak signal (RSSI < 5) → Move to window
  ├─ PDP fails → Check SIM plan, APN
  └─ Socket times out → Check server/firewall

Intermittent failures?
  ├─ Power supply too weak → Use 2A PSU
  ├─ Modem crashes → Check antenna
  └─ Queue overflow → Reduce send frequency
```

---

**Status**: Production ready
**Last updated**: 2025
**Support**: Full documentation in `/outputs` directory