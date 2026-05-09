/**
 * test_power_supply.c  —  Unit tests for Spark Analyzer power_supply module
 * ─────────────────────────────────────────────────────────────────────────────
 * Uses Unity test framework (included with ESP-IDF).
 *
 * Run: idf.py -T tests/unit_tests test
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "unity.h"
#include "power_supply.h"

/* ── Voltage rounding helpers ─────────────────────────────────────────────── */

void test_pps_round_exact_step(void) {
    /* Already a multiple of 100 mV — should be unchanged */
    TEST_ASSERT_EQUAL_UINT16(5000, ps_round_pps_voltage(5000));
    TEST_ASSERT_EQUAL_UINT16(3300, ps_round_pps_voltage(3300));
    TEST_ASSERT_EQUAL_UINT16(21000, ps_round_pps_voltage(21000));
    TEST_ASSERT_EQUAL_UINT16(9500, ps_round_pps_voltage(9500));
}

void test_pps_round_truncates(void) {
    /* Rounds down to nearest 100 mV step */
    TEST_ASSERT_EQUAL_UINT16(9500, ps_round_pps_voltage(9550));
    TEST_ASSERT_EQUAL_UINT16(9500, ps_round_pps_voltage(9599));
    TEST_ASSERT_EQUAL_UINT16(5000, ps_round_pps_voltage(5099));
    TEST_ASSERT_EQUAL_UINT16(12000, ps_round_pps_voltage(12000));
}

void test_avs_round_exact_step(void) {
    TEST_ASSERT_EQUAL_UINT16(15000, ps_round_avs_voltage(15000));
    TEST_ASSERT_EQUAL_UINT16(20000, ps_round_avs_voltage(20000));
    TEST_ASSERT_EQUAL_UINT16(28000, ps_round_avs_voltage(28000));
}

void test_avs_round_truncates(void) {
    TEST_ASSERT_EQUAL_UINT16(20000, ps_round_avs_voltage(20100));
    TEST_ASSERT_EQUAL_UINT16(20000, ps_round_avs_voltage(20199));
    TEST_ASSERT_EQUAL_UINT16(15000, ps_round_avs_voltage(15100));
}

/* ── Voltage range validation ─────────────────────────────────────────────── */

void test_pps_voltage_valid(void) {
    TEST_ASSERT_TRUE(ps_is_pps_voltage_valid(3300));
    TEST_ASSERT_TRUE(ps_is_pps_voltage_valid(5000));
    TEST_ASSERT_TRUE(ps_is_pps_voltage_valid(12000));
    TEST_ASSERT_TRUE(ps_is_pps_voltage_valid(21000));
}

void test_pps_voltage_invalid(void) {
    TEST_ASSERT_FALSE(ps_is_pps_voltage_valid(3299));   /* Below minimum */
    TEST_ASSERT_FALSE(ps_is_pps_voltage_valid(21001));  /* Above maximum */
    TEST_ASSERT_FALSE(ps_is_pps_voltage_valid(0));
    TEST_ASSERT_FALSE(ps_is_pps_voltage_valid(28000));  /* AVS range, not PPS */
}

void test_avs_voltage_valid(void) {
    TEST_ASSERT_TRUE(ps_is_avs_voltage_valid(15000));
    TEST_ASSERT_TRUE(ps_is_avs_voltage_valid(20000));
    TEST_ASSERT_TRUE(ps_is_avs_voltage_valid(28000));
}

void test_avs_voltage_invalid(void) {
    TEST_ASSERT_FALSE(ps_is_avs_voltage_valid(14999));
    TEST_ASSERT_FALSE(ps_is_avs_voltage_valid(28001));
    TEST_ASSERT_FALSE(ps_is_avs_voltage_valid(5000));   /* PPS range, not AVS */
}

/* ── Hardware limit constants ─────────────────────────────────────────────── */

void test_hardware_limits(void) {
    TEST_ASSERT_EQUAL_UINT16(3300,  PS_PPS_VMIN_MV);
    TEST_ASSERT_EQUAL_UINT16(21000, PS_PPS_VMAX_MV);
    TEST_ASSERT_EQUAL_UINT16(100,   PS_PPS_VSTEP_MV);
    TEST_ASSERT_EQUAL_UINT16(15000, PS_AVS_VMIN_MV);
    TEST_ASSERT_EQUAL_UINT16(28000, PS_AVS_VMAX_MV);
    TEST_ASSERT_EQUAL_UINT16(200,   PS_AVS_VSTEP_MV);
    TEST_ASSERT_EQUAL_UINT16(5000,  PS_IMAX_MA);
}

/* ── Test runner ─────────────────────────────────────────────────────────── */

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_pps_round_exact_step);
    RUN_TEST(test_pps_round_truncates);
    RUN_TEST(test_avs_round_exact_step);
    RUN_TEST(test_avs_round_truncates);
    RUN_TEST(test_pps_voltage_valid);
    RUN_TEST(test_pps_voltage_invalid);
    RUN_TEST(test_avs_voltage_valid);
    RUN_TEST(test_avs_voltage_invalid);
    RUN_TEST(test_hardware_limits);

    UNITY_END();
}
