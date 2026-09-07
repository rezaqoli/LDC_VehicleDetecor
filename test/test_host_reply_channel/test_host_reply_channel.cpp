// test_host_reply_channel.cpp
#include <Arduino.h>
#include <unity.h>
#include <cstring>
#include "ReplyChannel.h"

class MockChannel : public IReplyChannel {
 public:
  int   count = 0;
  char  last_msg[160] = "";
  uint8_t last_num = 0xFF;
  void send(uint8_t num, const char *msg) override {
    ++count;
    last_num = num;
    if (msg) {
      strncpy(last_msg, msg, sizeof(last_msg) - 1);
      last_msg[sizeof(last_msg) - 1] = '\0';
    } else {
      last_msg[0] = '\0';
    }
  }
  CmdMedium medium() const override { return CmdMedium::WS; }
};

// Inline single-instance singletons (mimic what ReplyChannel.cpp does in
// production, but without linking the Arduino MqttHandler).
class TestWsChannel      : public IReplyChannel { public: void send(uint8_t, const char *) override {} CmdMedium medium() const override { return CmdMedium::WS; } };
class TestMqttChannel    : public IReplyChannel { public: void send(uint8_t, const char *) override {} CmdMedium medium() const override { return CmdMedium::MQTT; } };
class TestSerialChannel  : public IReplyChannel { public: void send(uint8_t, const char *) override {} CmdMedium medium() const override { return CmdMedium::SERIAL_AT; } };
static TestWsChannel     s_ws;
static TestMqttChannel   s_mqtt;
static TestSerialChannel s_serial;
IReplyChannel &wsReplyChannel()    { return s_ws; }
IReplyChannel &mqttReplyChannel()  { return s_mqtt; }
IReplyChannel &serialReplyChannel(){ return s_serial; }

void setUp(void) {}
void tearDown(void) {}

void test_mock_channel_stores_num_and_msg()
{
  MockChannel c;
  c.send(7, "hello");
  TEST_ASSERT_EQUAL_INT(1, c.count);
  TEST_ASSERT_EQUAL_UINT8(7, c.last_num);
  TEST_ASSERT_EQUAL_STRING("hello", c.last_msg);
}

void test_singletons_have_distinct_mediums()
{
  IReplyChannel &ws = wsReplyChannel();
  IReplyChannel &mq = mqttReplyChannel();
  IReplyChannel &sr = serialReplyChannel();
  TEST_ASSERT_NOT_EQUAL((int)ws.medium(), (int)mq.medium());
  TEST_ASSERT_NOT_EQUAL((int)mq.medium(), (int)sr.medium());
  TEST_ASSERT_NOT_EQUAL((int)ws.medium(), (int)sr.medium());
}

void test_send_null_does_not_crash()
{
  MockChannel c;
  c.send(0, nullptr);
  TEST_ASSERT_EQUAL_INT(1, c.count);
  TEST_ASSERT_EQUAL_STRING("", c.last_msg);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_mock_channel_stores_num_and_msg);
  RUN_TEST(test_singletons_have_distinct_mediums);
  RUN_TEST(test_send_null_does_not_crash);
  return UNITY_END();
}
