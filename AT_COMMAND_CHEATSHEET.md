# EC200U AT Command Quick Reference

## Essential AT Commands for Vehicle Detection Integration

### 1. DEVICE MANAGEMENT

```
AT                          Test AT interface → OK
ATE0                        Disable echo (recommended)
ATE1                        Enable echo
ATI                         Get device info
AT+CGMI                      Get manufacturer
AT+CGMM                      Get model
AT+CGMR                      Get firmware version
ATZ                          Software reset
AT+QPOWD=1                   Power off
```

**Example Response**:
```
AT+CGMI
Quectel
OK
```

---

### 2. SIM CARD & IDENTITY

```
AT+CPIN?                     Check SIM status
AT+CPIN="1234"               Enter SIM PIN
AT+CGSN                      Get IMEI (device ID)
AT+CIMI                      Get IMSI (SIM ID)
AT+CCID                      Get ICCID (SIM serial)
```

**Expected Responses**:
```
AT+CPIN?
+CPIN: READY        ← SIM ready (no PIN)
OK

AT+CGSN
864391033987322              ← IMEI number
OK

AT+CIMI
310150123456789              ← IMSI (MCC=310, MNC=150)
OK
```

---

### 3. NETWORK REGISTRATION

```
AT+CREG=0                    Disable registration unsolicited results
AT+CREG=1                    Enable registration unsolicited results
AT+CREG?                     Check registration status
AT+COPS=0,2                  Automatic operator selection (2G)
AT+COPS=0,0                  Automatic operator selection (any)
AT+COPS?                     Get current operator
```

**Status Codes**:
- `0` = Not registered, not searching
- `1` = Registered, home network
- `2` = Not registered, searching
- `5` = Registered, roaming

**Example**:
```
AT+CREG?
+CREG: 0,1,34F4,0B3D0810  ← Registered (stat=1), LAC=34F4, CI=0B3D0810
OK

AT+COPS?
+COPS: 0,0,"Vodafone IT",2  ← Auto mode, operator="Vodafone IT", AcT=2 (UTRA)
OK
```

---

### 4. SIGNAL QUALITY

```
AT+CSQ                       Get 2G/3G signal (RSSI & BER)
AT+QCSQ                      Get LTE/NB-IoT detailed signal
```

**Response Format**:
```
AT+CSQ
+CSQ: 18,99              ← RSSI=18 (-93dBm), BER=99 (unknown)
OK

AT+QCSQ
+QCSQ: "LTE",-94,-12,13  ← LTE: RSRP=-94dBm, RSRQ=-12dB, SINR=13dB
OK
```

**Signal Quality Scale**:
- RSSI: 0-31 (0=<-113dBm, 31=>-51dBm, 99=not known)
  - Acceptable: >5
  - Good: >10
  - Excellent: >20
- BER: 0-7 (lower is better, 99=not known)
- RSRP (LTE): -140 to -44 dBm
  - Poor: < -120 dBm
  - Fair: -120 to -100 dBm
  - Good: -100 to -80 dBm
  - Excellent: > -80 dBm

---

### 5. PDP CONTEXT (DATA CONNECTION)

```
AT+CGDCONT=1,"IP","internet"     Define PDP context #1
AT+CGACT=1,1                      Activate context #1
AT+CGACT?                         Check active contexts
AT+CGACT=0,1                      Deactivate context #1
AT+CGPADDR=1                      Get IP address
```

**Example Sequence**:
```
AT+CGDCONT=1,"IP","internet"
OK

AT+CGACT=1,1
OK

AT+CGACT?
+CGACT: 1,1              ← Context 1 is active
OK

AT+CGPADDR=1
+CGPADDR: 1,"10.200.100.50"  ← Got IP address
OK
```

---

### 6. TCP/UDP SOCKETS (QUECTEL EXTENSION)

#### Open Socket
```
AT+QIOPEN=<contextID>,<connectID>,"TCP/UDP",<remoteIP/Domain>,<remotePort>

Examples:
AT+QIOPEN=1,0,"TCP","api.example.com",80
AT+QIOPEN=1,1,"UDP","8.8.8.8",53
AT+QIOPEN=1,2,"TCP","192.168.1.1",9000
```

**Response**: `+QIOPEN: <connectID>,<err>`
- err=0: Success
- err=-1: Failed
- err=550: Already open
- err=551: Operation not allowed

#### Send Data
```
AT+QISEND=<connectID>,<sendLen>

Example:
AT+QISEND=0,11
> Hello World
OK
```

#### Receive Data
```
AT+QIRECV=<connectID>,<recvLen>

Example:
AT+QIRECV=0,1024
+QIRECV: 50
<50 bytes of data here>
OK
```

#### Close Socket
```
AT+QICLOSE=<connectID>

Example:
AT+QICLOSE=0
OK
```

#### Check Socket Status
```
AT+QISTATE

Response: +QISTATE: <connectID>,<state>,<service_type>,<IP_address>,<remote_port>,<local_port>,<socket_state>
```

---

### 7. HTTP REQUESTS (OPTIONAL)

```
AT+QHHTTP=1                      Enable HTTP
AT+QHTTPCFG="contextid",1        Configure context
AT+QHTTPGET="http://example.com" Execute GET request
AT+QHTTPPOST="http://example.com",<dataLen>  Execute POST
```

---

### 8. DNS

```
AT+QIDNSCFG=1,"8.8.8.8","8.8.4.4"   Configure DNS
AT+QIDNSGIP="google.com"             Resolve hostname
```

---

### 9. POWER MANAGEMENT

```
AT+QSCLK=0                   Disable sleep mode
AT+QSCLK=1                   Enable sleep mode
AT+CFUN=0                    Minimum functionality (sleep)
AT+CFUN=1                    Full functionality
```

---

### 10. ERROR HANDLING & DEBUGGING

```
AT+CMEE=0                    Disable error codes (return ERROR)
AT+CMEE=1                    Enable numeric error codes
AT+CMEE=2                    Enable text error codes

AT+CEER                      Get extended error report
```

**Common Error Codes**:
- `+CME ERROR: 1` = Illegal command
- `+CME ERROR: 3` = Operation not allowed
- `+CME ERROR: 11` = SIM PIN required
- `+CME ERROR: 12` = SIM PUK required
- `+CME ERROR: 30` = No network service
- `+CME ERROR: 31` = Network timeout
- `+CME ERROR: 50` = Unknown error

---

### 11. CONFIGURATION QUERIES

```
AT+QCFG="nwscanmode"         Get network scan mode
AT+QCFG="nwscanmode",0       Set auto scan mode
AT+QCFG="nwscanmode",1       Set 2G only
AT+QCFG="nwscanmode",2       Set 4G only

AT+QCFG="band"               Get supported bands
AT+QCFG="ledmode"            LED control
AT+QCFG="sleepind"           Sleep indication
```

---

## Complete Connection Sequence

### For Data Transfer:

```
1. AT
   OK

2. ATE0
   OK

3. AT+CPIN?
   +CPIN: READY
   OK

4. AT+CREG?
   +CREG: 0,1,"34F4","0B3D0810"
   OK

5. AT+COPS?
   +COPS: 0,0,"Vodafone IT",2
   OK

6. AT+CSQ
   +CSQ: 18,99
   OK

7. AT+CGDCONT=1,"IP","internet"
   OK

8. AT+CGACT=1,1
   OK

9. AT+CGPADDR=1
   +CGPADDR: 1,"10.200.100.50"
   OK

10. AT+QIOPEN=1,0,"TCP","api.server.com",80
    +QIOPEN: 0,0
    OK

11. AT+QISEND=0,50
    > {"type":"vehicle","speed":65.5}...
    OK

12. AT+QIRECV=0,1024
    +QIRECV: 100
    <response data>
    OK

13. AT+QICLOSE=0
    OK
```

---

## Testing Socket Connectivity

### HTTP GET Test:
```
AT+QIOPEN=1,0,"TCP","google.com",80
+QIOPEN: 0,0
OK

AT+QISEND=0,44
> GET / HTTP/1.1\r\nHost: google.com\r\n\r\n

AT+QIRECV=0,1024
+QIRECV: 342
HTTP/1.1 301 Moved Permanently
Content-Type: text/html; charset=UTF-8
...
```

### UDP Test (DNS):
```
AT+QIOPEN=1,1,"UDP","8.8.8.8",53
+QIOPEN: 1,0
OK

AT+QISEND=1,<DNS_query_length>
> <DNS query data>

AT+QIRECV=1,512
+QIRECV: <response_length>
<DNS response>
```

---

## Troubleshooting Commands

```
AT+CGREG?                    Get GPRS registration (3G)
AT+CEREG?                    Get E-UTRA registration (LTE)
AT+COPS=4,2                  Get list of operators (may take 30+ seconds)
AT+CEER                      Get extended error info
AT+QSIMSTAT?                 Check SIM status
AT+QIDENTITY                 Get device identity
AT+QHARDINFO                 Get hardware info
```

---

## Protocol Tips

### AT Response Timeout Values:
- SIM commands (AT+CPIN): 1000ms
- Network queries (AT+CREG): 1000ms
- Socket operations: 5000ms
- Signal queries: 500ms
- Data send: 5000ms
- Data receive: 2000ms

### Safe Defaults for Vehicle Detection:
```
// Disable echo - cleaner responses
ATE0

// Set network to LTE preferred
AT+QCFG="nwscanmode",2

// Enable unsolicited registration notifications
AT+CREG=1

// Enable CME error codes for debugging
AT+CMEE=2

// Set socket send/receive to non-blocking
AT+QIMODE=0
```

---

## Performance Notes

- **First network registration**: 10-30 seconds
- **Re-registration after power cycle**: 5-15 seconds  
- **PDP context activation**: 1-5 seconds
- **Socket open**: 0.5-2 seconds
- **DNS lookup**: 1-3 seconds
- **HTTP request/response**: 2-10 seconds
- **RSSI query response time**: 100-500ms

---

## Memory & Buffer Info

- **RX Buffer**: Default 2048 bytes (configurable)
- **TX Buffer**: Default 512 bytes (configurable)
- **Max packet size**: ~1500 bytes per socket
- **Max concurrent sockets**: 10

---

## References

- **Quectel EC200U Datasheet**: Contains full AT command set
- **EC200U LTE Module AT Command Manual**: Comprehensive reference
- **3GPP TS 27.007**: Standard AT command definitions
- **3GPP TS 27.005**: AT commands for SMS and CBM

---

**Pro Tips**:
1. Always use `ATE0` to disable echo for cleaner parsing
2. Monitor signal strength continuously with periodic `AT+CSQ` queries
3. Use short timeouts (1-2 seconds) for queries, longer (5+ seconds) for data transfers
4. Implement connection pooling: keep sockets open between transfers
5. Set up unsolicited response handlers for network events (`AT+CREG=1`)
6. Test with `telnet` or serial monitor to debug AT commands before coding