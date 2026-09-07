// ============================================================
// ReplyChannel.h  —  Transport-agnostic reply sink for system
// commands. Implementations route to WebSocket, MQTT, or Serial.
// ============================================================
#pragma once
#include <stdint.h>

enum class CmdMedium : uint8_t
{
  WS        = 0,
  MQTT      = 1,
  SERIAL_AT = 2,   // avoid clash with Arduino's SERIAL macro
  UNKNOWN   = 0xFF
};

class IReplyChannel
{
 public:
  virtual ~IReplyChannel() = default;

  // Send a reply to the originating transport.
  // `num` is the WS client id for WS medium; ignored for MQTT/SERIAL.
  virtual void send(uint8_t num, const char *msg) = 0;

  virtual CmdMedium medium() const = 0;
};

// Singleton accessors. Cheap to call; no allocation.
IReplyChannel &wsReplyChannel();
IReplyChannel &mqttReplyChannel();
IReplyChannel &serialReplyChannel();