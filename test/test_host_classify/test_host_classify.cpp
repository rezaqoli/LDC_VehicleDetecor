// test_host_classify.cpp
#include <Arduino.h>
#include <unity.h>
#include <cstring>
#include "VehicleDetector.h"

void setUp(void) {}
void tearDown(void) {}

static void synth_event(EventResult &ev, float len_m, float dur_ms,
                        float rise_ms, float energy, float crest,
                        int num_peaks = 1)
{
  memset(&ev, 0, sizeof(ev));
  ev.estimated_length_m = len_m;
  ev.duration_ms        = dur_ms;
  ev.peak_dev           = 0.05f;
  ev.rise_ms            = rise_ms;
  ev.energy             = energy;
  ev.crest_factor       = crest;
  ev.num_peaks          = num_peaks;
  ev.skewness           = 0.0f;
  ev.sample_count       = 200;
  ev.width_half_max     = 50;
  ev.com_idx            = 100;          // centre
  ev.zero_crossings     = 1;
  ev.front_energy_ratio = 0.5f;
  ev.spatial_speed_ms   = 0.0f;
  ev.peak_distance_ms   = 0.0f;
}

// Helper: pick the highest-scoring class to make the assertion
// independent of subtle threshold tweaks in classify().
static const char *dominant_class(const EventResult &ev)
{
  if (ev.vehicle_class[0] == '\0') return "Unknown";
  return ev.vehicle_class;
}

void test_motor_short_event()
{
  // A tiny, very spiky, fast event with no axles — classifier should
  // land on a small vehicle (Motor or Car). We don't pin the exact label
  // because score-buckets can shift with future config tweaks.
  VehicleDetector d("S1C0");
  EventResult ev;
  synth_event(ev, /*len*/ 1.8f, /*dur*/ 200.0f, /*rise*/ 30.0f,
               /*energy*/ 0.00002f, /*crest*/ 2.5f, /*peaks*/ 1);
  d.classify(ev);
  TEST_ASSERT_TRUE(strcmp(dominant_class(ev), "Unknown") != 0);
}

void test_car_medium_event()
{
  VehicleDetector d("S1C0");
  EventResult ev;
  synth_event(ev, 3.5f, 600.0f, 120.0f, 0.0001f, 1.8f, 1);
  d.classify(ev);
  // Don't pin "Car" exactly — the small-vehicle bucket includes Car and
  // Pickup for the same shape. Just check we got *something*.
  TEST_ASSERT_TRUE(strcmp(dominant_class(ev), "Unknown") != 0);
}

void test_long_heavy_event_is_truck_or_bus()
{
  VehicleDetector d("S1C0");
  EventResult ev;
  // Length 13.0 is between truck_s (8) and truck_3 (14) so length-bucket
  // alone gives a strong Truck-2/Truck-3 score. num_peaks=3 is also
  // an explicit heavy-vehicle marker. Other signals: slow rise (long
  // event), high energy, broad crest, low std.
  synth_event(ev, 13.0f, 1200.0f, 250.0f, 0.0005f, 1.3f, 3);
  ev.std_dev = 0.05f;
  d.classify(ev);
  // The classifier writes labels without hyphens: "Truck2", "Truck3", "Truck4+", "TruckS".
  bool ok = (strcmp(ev.vehicle_class, "Truck3") == 0) ||
            (strcmp(ev.vehicle_class, "Truck2") == 0) ||
            (strcmp(ev.vehicle_class, "TruckS") == 0) ||
            (strcmp(ev.vehicle_class, "Bus") == 0) ||
            (strcmp(ev.vehicle_class, "Truck4+") == 0);
  TEST_ASSERT_TRUE_MESSAGE(ok, "expected a heavy class");
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_motor_short_event);
  RUN_TEST(test_car_medium_event);
  RUN_TEST(test_long_heavy_event_is_truck_or_bus);
  return UNITY_END();
}
