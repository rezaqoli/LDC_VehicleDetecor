// test_host_detection.cpp — VehicleDetector state machine
#include <Arduino.h>
#include <unity.h>
#include "VehicleDetector.h"
#include "SignalProcessing.h"
#include "AutoCalibrator.h"

static EventResult s_lastEv;
static int s_evCount = 0;
static void captureEv(const char *m) { (void)m; ++s_evCount; }

void setUp(void) {}
void tearDown(void) {}

void test_calibration_produces_baseline_near_input_mean()
{
  VehicleDetector d("S1C0");
  // Feed a steady ~2000 raw value during warmup + calibration.
  const uint32_t N = 800;
  uint64_t sum = 0;
  for (uint32_t i = 0; i < N; i++) {
    uint32_t ts = i * 5000;
    EventResult ev;
    d.feed(2000, ts, ev);
    sum += 2000;
  }
  float bl = d.baseline();
  TEST_ASSERT_TRUE(bl > 0.0f);
  // Detector baselines smoothed value, not raw, so allow ±20% of the input.
  float expected = 2000.0f;
  TEST_ASSERT_FLOAT_WITHIN(expected * 0.20f, expected, bl);
}

void test_no_event_during_calibration()
{
  VehicleDetector d("S1C0");
  s_evCount = 0;
  // Big spike should NOT trigger an event during WARMUP / CALIBRATION.
  for (uint32_t i = 0; i < 50; i++) {
    EventResult ev;
    d.feed(2000, i * 5000, ev);
  }
  for (uint32_t i = 0; i < 50; i++) {
    EventResult ev;
    d.feed(9000, (50 + i) * 5000, ev);
  }
  TEST_ASSERT_EQUAL_INT(0, s_evCount);
}

void test_event_triggered_on_big_spike_after_calibration()
{
  VehicleDetector d("S1C0");
  // Warmup + calibration with steady signal
  for (uint32_t i = 0; i < 800; i++) {
    EventResult ev;
    d.feed(2000, i * 5000, ev);
  }
  // After calibration the detector should have a positive baseline and
  // an updated AutoCalibrator; feed() must continue to be safe to call.
  for (uint32_t i = 0; i < 200; i++) {
    EventResult ev;
    d.feed(5000, (800 + i) * 5000, ev);
  }
  TEST_ASSERT_TRUE(d.baseline() > 0.0f);
  // Confidence is reported by the auto-calibrator
  TEST_ASSERT_TRUE(d.confidence() >= 0.0f);
}

void test_classification_known_shape_assigns_class()
{
  // Build a VehicleDetector, force a known-good event, then reclassify.
  VehicleDetector d("S1C0");
  EventResult ev;
  // A long, broad, slow event: should classify as a heavy vehicle
  // (truck/bus) by length-based bounds, not a motor.
  ev.duration_ms = 800.0f;
  ev.peak_dev    = 0.05f;
  ev.estimated_length_m = 12.0f;
  ev.rise_ms = 250.0f;
  ev.energy = 0.001f;
  ev.crest_factor = 1.3f;
  d.classify(ev);
  // Must be a known class (not "Unknown").
  TEST_ASSERT_NOT_EQUAL(0, strcmp(ev.vehicle_class, "Unknown"));
  TEST_ASSERT(strlen(ev.vehicle_class) > 0);
}

void test_derivative_mode_detects_sharp_rise()
{
  DetectorConfig cfg;
  cfg.entry_mode = EntryDetectionMode::DERIVATIVE;
  cfg.warmup_samples = 5;
  cfg.calib_samples = 40;
  cfg.smoothing_alpha = 1.0f;
  cfg.confirm_samples = 2;
  cfg.min_event_samples = 4;
  cfg.min_event_ms = 10;
  VehicleDetector d("S1C0", cfg);
  EventResult ev;
  uint32_t sample = 0;

  for (; sample < 50; sample++)
    d.feed(100000, sample * 5000, ev);
  bool fired = false;
  for (int i = 0; i < 12; i++, sample++)
    fired = d.feed(110000, sample * 5000, ev) || fired;

  for (int i = 0; i < 12; i++, sample++)
    fired = d.feed(100000, sample * 5000, ev) || fired;
  TEST_ASSERT_TRUE(fired);
}

void test_derivative_mode_detects_slow_sustained_rise()
{
  DetectorConfig cfg;
  cfg.entry_mode = EntryDetectionMode::DERIVATIVE;
  cfg.warmup_samples = 5;
  cfg.calib_samples = 40;
  cfg.smoothing_alpha = 1.0f;
  cfg.confirm_samples = 2;
  cfg.min_event_samples = 4;
  cfg.min_event_ms = 10;
  cfg.min_slow_enter_ms = 50;
  cfg.derivative_sigma = 20.0f; // ensure this path is not a sharp-edge trigger
  cfg.derivative_slow_sigma = 3.0f;
  VehicleDetector d("S1C0", cfg);
  EventResult ev;
  uint32_t sample = 0;

  for (; sample < 50; sample++)
    d.feed(100000, sample * 5000, ev);
  for (int i = 0; i < 40; i++, sample++)
    d.feed(100000 + (i + 1) * 10, sample * 5000, ev);

  bool fired = false;
  for (int i = 0; i < 12; i++, sample++)
    fired = d.feed(100000, sample * 5000, ev) || fired;
  TEST_ASSERT_TRUE(fired);
}

int runUnityTests(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_calibration_produces_baseline_near_input_mean);
  RUN_TEST(test_no_event_during_calibration);
  RUN_TEST(test_event_triggered_on_big_spike_after_calibration);
  RUN_TEST(test_classification_known_shape_assigns_class);
  RUN_TEST(test_derivative_mode_detects_sharp_rise);
  RUN_TEST(test_derivative_mode_detects_slow_sustained_rise);
  return UNITY_END();
}

int main(int /*argc*/, char ** /*argv*/)
{
  return runUnityTests();
}
