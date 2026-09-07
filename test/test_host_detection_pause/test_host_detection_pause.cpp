// test_host_detection_pause.cpp
#include <Arduino.h>
#include <unity.h>
#include "DetectionControl.h"

void setUp(void) { detectionSetPaused(false); }
void tearDown(void) {}

void test_default_state_is_running()
{
  TEST_ASSERT_FALSE(detectionIsPaused());
  TEST_ASSERT_EQUAL_STRING("RUNNING", detectionStateName());
}

void test_pause_then_resume()
{
  detectionSetPaused(true);
  TEST_ASSERT_TRUE(detectionIsPaused());
  TEST_ASSERT_EQUAL_STRING("PAUSED", detectionStateName());
  detectionSetPaused(false);
  TEST_ASSERT_FALSE(detectionIsPaused());
  TEST_ASSERT_EQUAL_STRING("RUNNING", detectionStateName());
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_default_state_is_running);
  RUN_TEST(test_pause_then_resume);
  return UNITY_END();
}
