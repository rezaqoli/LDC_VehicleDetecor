// ============================================================
// ReplyChannel.cpp
// ============================================================
#include "ReplyChannel.h"
#include "WsUtils.h"
#include "MqttHandler.h"

class WsReplyChannel : public IReplyChannel
{
 public:
  void send(uint8_t num, const char *msg) override
  {
    if (msg) wsSendToClient(num, msg);
  }
  CmdMedium medium() const override { return CmdMedium::WS; }
};

class MqttReplyChannel : public IReplyChannel
{
 public:
  void send(uint8_t /*num*/, const char *msg) override
  {
    if (msg) mqttPublishResponse(msg);
  }
  CmdMedium medium() const override { return CmdMedium::MQTT; }
};

class SerialReplyChannel : public IReplyChannel
{
 public:
  void send(uint8_t /*num*/, const char *msg) override
  {
    if (msg) Serial.println(msg);
  }
  CmdMedium medium() const override { return CmdMedium::SERIAL_AT; }
};

static WsReplyChannel   s_ws;
static MqttReplyChannel s_mqtt;
static SerialReplyChannel s_serial;

IReplyChannel &wsReplyChannel()    { return s_ws; }
IReplyChannel &mqttReplyChannel()  { return s_mqtt; }
IReplyChannel &serialReplyChannel(){ return s_serial; }
