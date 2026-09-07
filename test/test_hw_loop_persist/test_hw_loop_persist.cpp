// test_hw_loop_persist.cpp — on-device, exercises NVS
#include <Arduino.h>
#include <unity.h>
#include "PersistentConfig.h"

void setUp(void)
{
  TEST_ASSERT_TRUE(PersistentConfig::init());
}

void tearDown(void) {}

void test_loop_config_roundtrip()
{
  LoopConfig a; a.dualLoop = true; a.distance = 1.23f;
  a.sensor1 = 0; a.ch1 = 1; a.sensor2 = 1; a.ch2 = 2;
  TEST_ASSERT_TRUE(PersistentConfig::saveLoopConfig(0, a));
  LoopConfig b;
  TEST_ASSERT_TRUE(PersistentConfig::loadLoopConfig(0, b));
  TEST_ASSERT_EQUAL(a.dualLoop, b.dualLoop);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.distance, b.distance);
  TEST_ASSERT_EQUAL_UINT8(a.sensor1, b.sensor1);
  TEST_ASSERT_EQUAL_UINT8(a.ch1,     b.ch1);
  TEST_ASSERT_EQUAL_UINT8(a.sensor2, b.sensor2);
  TEST_ASSERT_EQUAL_UINT8(a.ch2,     b.ch2);
}

void test_sms_contacts_roundtrip()
{
  TEST_ASSERT_TRUE(PersistentConfig::saveSmsContacts("+989121|+989122"));
  char buf[64] = "";
  TEST_ASSERT_TRUE(PersistentConfig::loadSmsContacts(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("+989121|+989122", buf);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_loop_config_roundtrip);
  RUN_TEST(test_sms_contacts_roundtrip);
  return UNITY_END();
}
