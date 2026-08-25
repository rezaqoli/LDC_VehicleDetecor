// ============================================================
// Quectel EC200U LTE Module Driver
// AT Command Interface for ESP32-S3
// ============================================================
#pragma once
#ifndef EC200U_LTE_H
#define EC200U_LTE_H

#define TINY_GSM_MODEM_QUECTEL
#define TINY_GSM_RX_BUFFER 1024
#define TINY_GSM_USE_GPRS true


#include <Arduino.h>
#include <HardwareSerial.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

// ========== Configuration ==========
#define EC200U_RX_PIN 18          // RX pin (GPIO0)
#define EC200U_TX_PIN 17         // TX pin (GPIO1)
//#define EC200U_PWR_PIN 2         // Power control pin
#define EC200U_RESET_PIN 8      // Reset pin
#define EC200U_BAUD_RATE 115200  // Default baud rate
#define EC200U_SERIAL_NUM 1      // UART1 on ESP32-S3

// Response timeout
#define AT_CMD_TIMEOUT_MS 5000
#define AT_BOOT_TIMEOUT_MS 30000

// Buffer sizes
#define AT_RX_BUFFER_SIZE 512
#define AT_TX_BUFFER_SIZE 160
#define AT_RESPONSE_LINES 8
#define AT_RESPONSE_LINE_SIZE 128

// ========== Data Types ==========
enum class EC200UState
{
  OFFLINE = 0,
  INITIALIZING = 1,
  IDLE = 2,
  CONNECTED_2G = 3,
  CONNECTED_4G = 4,
  ERROR_STATE = 5
};

enum class NetworkType
{
  UNKNOWN = 0,
  GSM_2G = 1,
  LTE_4G = 2,
  NBIOT = 3
};

struct SignalQuality
{
  uint8_t rssi;      // Signal strength (0-31, higher is better)
  uint8_t ber;       // Bit error rate (0-7)
  int16_t rsrp;      // LTE Reference Signal Power (-140 to -44 dBm)
  int16_t rsrq;      // LTE Reference Signal Quality (-20 to -3 dB)
  int16_t sinr;      // LTE Signal to Interference Noise Ratio (-20 to 30 dB)
};

struct NetworkInfo
{
  NetworkType type;
  bool registered;
  bool roaming;
  uint8_t lac[4];    // Location Area Code
  uint8_t ci[8];     // Cell ID
  SignalQuality signal;
  char operator_name[32];
};

struct DeviceInfo
{
  char manufacturer[64];
  char model[64];
  char fw_version[64];
  char imei[16];
  char imsi[16];
};

struct ATResponse
{
  bool success;
  uint16_t line_count;
  char lines[AT_RESPONSE_LINES][AT_RESPONSE_LINE_SIZE];
  uint32_t response_time_ms;
};

// ========== EC200U Driver Class ==========
class EC200U_LTE
{
public:
  EC200U_LTE();
  ~EC200U_LTE();

  // ===== Initialization =====
  bool begin();
  void end();
  bool isInitialized() const { return initialized; }

  // ===== Basic AT Commands =====
  bool atTest();                              // AT
  bool atEcho(bool enable);                   // ATE
  bool getDeviceInfo(DeviceInfo &info);       // ATI, +CGSN
  bool getNetworkInfo(NetworkInfo &info);     // +CREG, +COPS, +CSQ, +QCSQ
  bool reset(bool hardReset = false);         // ATZ or hard reset

  // ===== Network Registration =====
  bool queryRegistrationStatus(NetworkInfo &info);
  bool waitForRegistration(uint32_t timeout_ms = 30000);
  bool setNetworkMode(NetworkType prefer);    // 2G/4G preference
  bool getSignalQuality(SignalQuality &quality);

  // ===== SIM Card =====
  bool getSIMStatus();
  bool checkPIN();                            // +CPIN
  bool enterPIN(const char *pin);

  // ===== Data Connection =====
  bool activatePDP(const char *apn);         // +CGACT, +CGDCONT
  bool deactivatePDP();
  bool getPDPStatus();

  // ===== Socket Commands (TCP/UDP) =====
  int32_t openSocket(const char *host, uint16_t port, bool is_udp = false);
  bool closeSocket(int32_t socket_id);
  bool sendData(int32_t socket_id, const uint8_t *data, uint16_t len);
  uint16_t receiveData(int32_t socket_id, uint8_t *buffer, uint16_t max_len);
  bool setReceiveCallback(int32_t socket_id, void (*callback)(const uint8_t *, uint16_t));

  // ===== Power Management =====
  bool setPowerMode(uint8_t mode);           // 0=full, 1=sleep, 2=deep
  bool sleep();
  bool wakeup();

  // ===== Low-level AT Interface =====
  ATResponse sendCommand(const char *cmd, uint32_t timeout_ms = AT_CMD_TIMEOUT_MS);
  ATResponse sendCommandFmt(uint32_t timeout_ms, const char *fmt, ...);
  void flushBuffer();

  // ===== State Management =====
  EC200UState getState() const { return state; }
  const char *getStateString() const;
  uint32_t getLastResponseTime() const { return last_response_time_ms; }

  // ===== Debug =====
  void enableDebug(bool enable) { debug_enabled = enable; }
  void printDebug(const char *fmt, ...);

private:
  HardwareSerial *serial;
  EC200UState state;
  bool initialized;
  bool debug_enabled;
  uint32_t last_response_time_ms;
  uint32_t boot_time_ms;

  // ===== Internal Helpers =====
  bool waitForResponse(char *buffer, size_t max_len, uint32_t timeout_ms);
  bool parseATResponse(const char *response, ATResponse &result);
  bool powerOn();
  bool powerOff();
  bool hardReset();

  // ===== Parsing Helpers =====
  static bool extractStringParam(const char *line, uint8_t param_idx, char *out, size_t out_len);
  static bool extractIntParam(const char *line, uint8_t param_idx, int32_t &out);
  static bool extractHexParam(const char *line, uint8_t param_idx, uint32_t &out);
};

#endif // EC200U_LTE_H
