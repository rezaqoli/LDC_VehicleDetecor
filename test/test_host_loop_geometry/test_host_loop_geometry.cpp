// test_host_loop_geometry.cpp
#include <Arduino.h>
#include <unity.h>
#include "LoopGeometry.h"

void setUp(void) {}
void tearDown(void) {}

void test_adjacency_two_lanes_apart()
{
  LoopGeometryManager mgr;
  mgr.reset();
  // 4 loops in a 4-lane road, x separated by lane width 3.5 m.
  float y = 5.0f;
  for (uint8_t i = 0; i < 4; i++) {
    LoopGeometry lg(0, i, i, /*x*/ i * 3.5f, /*y*/ y, "");
    mgr.setLoop(i, lg);
  }
  mgr.computeAdjacency();
  // Lane 0 <-> Lane 1 must be adjacent; 0 <-> 2 should not.
  int a01 = mgr.findLoopIndex(0, 0);
  int a11 = mgr.findLoopIndex(0, 1);
  int a21 = mgr.findLoopIndex(0, 2);
  TEST_ASSERT_TRUE(mgr.areAdjacent(a01, a11));
  TEST_ASSERT_FALSE(mgr.areAdjacent(a01, a21));
}

void test_get_loop_count_after_load_defaults()
{
  LoopGeometryManager mgr;
  mgr.loadDefaults();
  TEST_ASSERT_TRUE(mgr.getLoopCount() > 0);
}

void test_find_loop_returns_null_for_unknown()
{
  LoopGeometryManager mgr;
  mgr.reset();
  const LoopGeometry *p = mgr.findLoop(1, 3);
  TEST_ASSERT_NULL(p);
}

void test_set_loop_then_get_loop_roundtrip()
{
  LoopGeometryManager mgr;
  mgr.reset();
  LoopGeometry lg(0, 2, 1, 7.0f, 4.0f, "S1C3");
  mgr.setLoop(2, lg);
  LoopGeometry out = mgr.getLoop(2);
  TEST_ASSERT_EQUAL_UINT8(0, out.sensor_id);
  TEST_ASSERT_EQUAL_UINT8(2, out.channel_id);
  TEST_ASSERT_EQUAL_UINT8(1, out.lane_id);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.0f, out.x_m);
  TEST_ASSERT_EQUAL_STRING("S1C3", out.name);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_adjacency_two_lanes_apart);
  RUN_TEST(test_get_loop_count_after_load_defaults);
  RUN_TEST(test_find_loop_returns_null_for_unknown);
  RUN_TEST(test_set_loop_then_get_loop_roundtrip);
  return UNITY_END();
}
