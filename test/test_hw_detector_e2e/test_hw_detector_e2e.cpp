// test_hw_detector_e2e.cpp — on-device, two paired detectors
#include <Arduino.h>
#include <unity.h>
#include "VehicleDetector.h"
#include "LoopGeometry.h"

void setUp(void) {}
void tearDown(void) {}

void test_two_paired_detectors_drive_simulated_vehicle()
{
  VehicleDetector a("S1C0");
  VehicleDetector b("S2C0");
  EventResult ea, eb;
  // Warmup + calibration with 1500 raw units.
  for (uint32_t i = 0; i < 800; i++) {
    a.feed(1500, i * 5000, ea);
    b.feed(1500, i * 5000, eb);
  }
  // Vehicle passes A then B; raise + drop.
  bool aFired = false, bFired = false;
  uint32_t t = 800;
  for (uint32_t i = 0; i < 30; i++) {
    EventResult ev;
    aFired = a.feed(3500, (t + i) * 5000, ev) || aFired;
  }
  t += 60;  // 300 ms gap
  for (uint32_t i = 0; i < 30; i++) {
    EventResult ev;
    bFired = b.feed(3500, (t + i) * 5000, ev) || bFired;
  }
  TEST_ASSERT_TRUE(aFired);
  TEST_ASSERT_TRUE(bFired);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_two_paired_detectors_drive_simulated_vehicle);
  return UNITY_END();
}
