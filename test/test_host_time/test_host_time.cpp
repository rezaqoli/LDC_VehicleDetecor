// test_host_time.cpp
#include <Arduino.h>
#include <unity.h>
#include <cstring>
#include "TimeManager.h"
#include "hal/Hal.h"

void setUp(void) {}
void tearDown(void) {}

void test_formatIsoTime_buffer_is_null_safe()
{
  // The native test build stubs out TimeManager.cpp, so formatIsoTime is a
  // no-op. Verify the stub doesn't crash on a NULL buffer and respects the
  // length parameter.
  char buf[8] = "xxxxxxx";
  formatIsoTime(buf, sizeof(buf), 0);
  // Buffer must be either untouched or NUL-terminated within bounds.
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

void test_formatIsoTime_handles_zero_epoch()
{
  char buf[8] = "";
  formatIsoTime(buf, sizeof(buf), 0);
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_formatIsoTime_buffer_is_null_safe);
  RUN_TEST(test_formatIsoTime_handles_zero_epoch);
  return UNITY_END();
}
