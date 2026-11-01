// test_jog_gate.cpp — native unit tests for the pure JogGate decision
// helper.
//
// Built only by [env:native] (platformio.ini), which compiles this file
// with no other source (JogGate.h is header-only, included directly), so
// this runs as a plain host executable with no Arduino, no ESP32, no
// physical hardware.

#include <unity.h>

#include "../../src/JogGate.h"

void setUp(void) {}
void tearDown(void) {}

// --- aheadIsMax() --------------------------------------------------------
// Four combinations of (jogIsForward, forwardIsTowardMax).

void test_ahead_is_max_when_forward_jog_and_forward_toward_max(void) {
  // Jogging FORWARD, and FORWARD is mapped toward MAX -> ahead is MAX.
  TEST_ASSERT_TRUE(JogGate::aheadIsMax(/*jogIsForward=*/true,
                                       /*forwardIsTowardMax=*/true));
}

void test_ahead_is_min_when_forward_jog_and_forward_toward_min(void) {
  // Jogging FORWARD, but FORWARD is mapped toward MIN -> ahead is MIN.
  TEST_ASSERT_FALSE(JogGate::aheadIsMax(/*jogIsForward=*/true,
                                        /*forwardIsTowardMax=*/false));
}

void test_ahead_is_min_when_reverse_jog_and_forward_toward_max(void) {
  // Jogging REVERSE while FORWARD is mapped toward MAX -> REVERSE heads
  // toward MIN, so ahead is MIN.
  TEST_ASSERT_FALSE(JogGate::aheadIsMax(/*jogIsForward=*/false,
                                        /*forwardIsTowardMax=*/true));
}

void test_ahead_is_max_when_reverse_jog_and_forward_toward_min(void) {
  // Jogging REVERSE while FORWARD is mapped toward MIN -> REVERSE heads
  // toward MAX, so ahead is MAX.
  TEST_ASSERT_TRUE(JogGate::aheadIsMax(/*jogIsForward=*/false,
                                       /*forwardIsTowardMax=*/false));
}

// --- mustRefuse() --------------------------------------------------------
// For each of the four (jogIsForward, forwardIsTowardMax) combinations
// above, covering: the ahead sensor engaged (must refuse), both clear
// (must not refuse), and only the behind sensor engaged (must never
// refuse).

// Combination 1: forward jog, FORWARD toward MAX -> ahead = MAX, behind = MIN.
void test_refuse_combo1_ahead_max_engaged(void) {
  bool ahead = JogGate::aheadIsMax(true, true);
  // Ahead (MAX) engaged, behind (MIN) clear -> must refuse.
  TEST_ASSERT_TRUE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/true));
}

void test_refuse_combo1_both_clear(void) {
  bool ahead = JogGate::aheadIsMax(true, true);
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/false));
}

void test_refuse_combo1_behind_only_engaged(void) {
  bool ahead = JogGate::aheadIsMax(true, true);
  // Behind (MIN) engaged, ahead (MAX) clear -> must never refuse.
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/true, /*maxActive=*/false));
}

// Combination 2: forward jog, FORWARD toward MIN -> ahead = MIN, behind = MAX.
void test_refuse_combo2_ahead_min_engaged(void) {
  bool ahead = JogGate::aheadIsMax(true, false);
  // Ahead (MIN) engaged, behind (MAX) clear -> must refuse.
  TEST_ASSERT_TRUE(JogGate::mustRefuse(ahead, /*minActive=*/true, /*maxActive=*/false));
}

void test_refuse_combo2_both_clear(void) {
  bool ahead = JogGate::aheadIsMax(true, false);
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/false));
}

void test_refuse_combo2_behind_only_engaged(void) {
  bool ahead = JogGate::aheadIsMax(true, false);
  // Behind (MAX) engaged, ahead (MIN) clear -> must never refuse.
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/true));
}

// Combination 3: reverse jog, FORWARD toward MAX -> ahead = MIN, behind = MAX.
void test_refuse_combo3_ahead_min_engaged(void) {
  bool ahead = JogGate::aheadIsMax(false, true);
  // Ahead (MIN) engaged, behind (MAX) clear -> must refuse.
  TEST_ASSERT_TRUE(JogGate::mustRefuse(ahead, /*minActive=*/true, /*maxActive=*/false));
}

void test_refuse_combo3_both_clear(void) {
  bool ahead = JogGate::aheadIsMax(false, true);
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/false));
}

void test_refuse_combo3_behind_only_engaged(void) {
  bool ahead = JogGate::aheadIsMax(false, true);
  // Behind (MAX) engaged, ahead (MIN) clear -> must never refuse.
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/true));
}

// Combination 4: reverse jog, FORWARD toward MIN -> ahead = MAX, behind = MIN.
void test_refuse_combo4_ahead_max_engaged(void) {
  bool ahead = JogGate::aheadIsMax(false, false);
  // Ahead (MAX) engaged, behind (MIN) clear -> must refuse.
  TEST_ASSERT_TRUE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/true));
}

void test_refuse_combo4_both_clear(void) {
  bool ahead = JogGate::aheadIsMax(false, false);
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/false, /*maxActive=*/false));
}

void test_refuse_combo4_behind_only_engaged(void) {
  bool ahead = JogGate::aheadIsMax(false, false);
  // Behind (MIN) engaged, ahead (MAX) clear -> must never refuse.
  TEST_ASSERT_FALSE(JogGate::mustRefuse(ahead, /*minActive=*/true, /*maxActive=*/false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_ahead_is_max_when_forward_jog_and_forward_toward_max);
  RUN_TEST(test_ahead_is_min_when_forward_jog_and_forward_toward_min);
  RUN_TEST(test_ahead_is_min_when_reverse_jog_and_forward_toward_max);
  RUN_TEST(test_ahead_is_max_when_reverse_jog_and_forward_toward_min);
  RUN_TEST(test_refuse_combo1_ahead_max_engaged);
  RUN_TEST(test_refuse_combo1_both_clear);
  RUN_TEST(test_refuse_combo1_behind_only_engaged);
  RUN_TEST(test_refuse_combo2_ahead_min_engaged);
  RUN_TEST(test_refuse_combo2_both_clear);
  RUN_TEST(test_refuse_combo2_behind_only_engaged);
  RUN_TEST(test_refuse_combo3_ahead_min_engaged);
  RUN_TEST(test_refuse_combo3_both_clear);
  RUN_TEST(test_refuse_combo3_behind_only_engaged);
  RUN_TEST(test_refuse_combo4_ahead_max_engaged);
  RUN_TEST(test_refuse_combo4_both_clear);
  RUN_TEST(test_refuse_combo4_behind_only_engaged);
  return UNITY_END();
}
