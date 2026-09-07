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
    if (!msg) return;
    char envelope[MQTT_PUB_PAYLOAD_MAX];
    const char *cmdId = mqttLastCmdId();
    if (cmdId && *cmdId)
    {
      // Per-board lane: wrap replies with RSP|<cmd_id>| so the dashboard
      // can correlate each reply with the CMD that triggered it.
      snprintf(envelope, sizeof(envelope), "RSP|%s|%s", cmdId, msg);
      mqttPublishResponseTo(mqttTopicResponsesBoard(), envelope);
      // Global lane: publish the correlated envelope as well. This allows a
      // fleet dashboard to map a response received on the global topic back
      // to the board that originated the command.
      mqttPublishResponseTo("vehicles/command_responses", envelope);
    }
    else
    {
      // Legacy command (no cmd_id) — fan out to both lanes (bare form).
      mqttPublishResponse(msg);
    }
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
