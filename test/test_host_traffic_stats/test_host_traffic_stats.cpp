// test_host_traffic_stats.cpp
#include <Arduino.h>
#include <unity.h>
#include "TrafficStats.h"

void setUp(void)
{
  trafficStatsInit();
  trafficStatsReset();
}

void tearDown(void) {}

void test_record_vehicle_updates_count_and_avg_speed()
{
  trafficStatsRecordVehicle(VCLASS_CAR, 60.0f);
  trafficStatsRecordVehicle(VCLASS_CAR, 80.0f);
  trafficStatsRecordVehicle(VCLASS_CAR, 100.0f);
  String report = trafficStatsBuildReport(false);
  TEST_ASSERT_TRUE(strstr(report.c_str(), "|total:3") != nullptr);
  TEST_ASSERT_TRUE(strstr(report.c_str(), "Car_cnt:3") != nullptr);
}

void test_speed_violation_above_limit()
{
  g_traffic_rules.speed_limit_kmh = 80.0f;
  g_traffic_rules.speed_tolerance_kmh = 5.0f;
  trafficStatsRecordVehicle(VCLASS_CAR, 120.0f);
  trafficStatsRecordSpeedViolation(VCLASS_CAR, 120.0f);
  String report = trafficStatsBuildReport(false);
  TEST_ASSERT_TRUE(strstr(report.c_str(), "|speed_viol:1") != nullptr);
}

void test_distance_violation_increments()
{
  trafficStatsRecordVehicle(VCLASS_CAR, 50.0f);
  trafficStatsRecordDistanceViolation(VCLASS_CAR);
  trafficStatsRecordDistanceViolation(VCLASS_CAR);
  String report = trafficStatsBuildReport(false);
  TEST_ASSERT_TRUE(strstr(report.c_str(), "|dist_viol:2") != nullptr);
}

void test_clear_after_resets_counters()
{
  trafficStatsRecordVehicle(VCLASS_CAR, 60.0f);
  String r1 = trafficStatsBuildReport(/*clear_after*/ true);
  TEST_ASSERT_TRUE(strstr(r1.c_str(), "|total:1") != nullptr);
  String r2 = trafficStatsBuildReport(false);
  TEST_ASSERT_TRUE(strstr(r2.c_str(), "|total:0") != nullptr);
}

void test_class_index_roundtrip()
{
  TEST_ASSERT_EQUAL_INT(VCLASS_BUS, vehicleClassIndexFromString("Bus"));
  TEST_ASSERT_EQUAL_STRING("Bus", vehicleClassIndexToString(VCLASS_BUS));
  TEST_ASSERT_EQUAL_INT(VCLASS_UNKNOWN, vehicleClassIndexFromString("Nonsense"));
  TEST_ASSERT_EQUAL_STRING("Unknown", vehicleClassIndexToString(VCLASS_UNKNOWN));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_record_vehicle_updates_count_and_avg_speed);
  RUN_TEST(test_speed_violation_above_limit);
  RUN_TEST(test_distance_violation_increments);
  RUN_TEST(test_clear_after_resets_counters);
  RUN_TEST(test_class_index_roundtrip);
  return UNITY_END();
}
