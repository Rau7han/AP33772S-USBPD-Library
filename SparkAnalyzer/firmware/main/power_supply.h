/*
 * power_supply.h / power_supply.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Programmable Power Supply (PPS) abstraction for Spark Analyzer.
 * Provides a clean interface for voltage/current control with parameter
 * validation and range clamping.
 *
 * Hardware capability (AP33772S + PPS-capable USB-C charger):
 *   PPS:  3.3 V – 21.0 V in 100 mV steps, current up to 5 A
 *   AVS:  15.0 V – 28.0 V in 200 mV steps, current up to 5 A
 *   Fixed: 5 V / 9 V / 12 V / 15 V / 20 V (charger-dependent)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_POWER_SUPPLY_H
#define SPARK_POWER_SUPPLY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── Hardware limits ──────────────────────────────────────────────────────── */
#define PS_PPS_VMIN_MV    3300u
#define PS_PPS_VMAX_MV   21000u
#define PS_PPS_VSTEP_MV    100u
#define PS_AVS_VMIN_MV   15000u
#define PS_AVS_VMAX_MV   28000u
#define PS_AVS_VSTEP_MV    200u
#define PS_IMAX_MA        5000u
#define PS_ISTEP_MA         50u   /* Minimum current adjustment granularity */

/** Power supply operating mode */
typedef enum {
    PS_MODE_FIXED = 0,  /**< Standard USB PD fixed voltage (5/9/12/15/20V) */
    PS_MODE_PPS   = 1,  /**< Programmable Power Supply (3.3V–21V, 100mV steps) */
    PS_MODE_AVS   = 2,  /**< Adjustable Voltage Supply (15V–28V, 200mV steps) */
} ps_mode_t;

/** Power supply state */
typedef struct {
    ps_mode_t mode;
    uint16_t  voltage_mv;       /**< Currently requested voltage */
    uint16_t  current_limit_ma; /**< Currently requested current limit */
    uint8_t   pdo_index;        /**< Active PDO index (1–13, 0=none) */
    bool      output_enabled;   /**< VOUT switch state */
} ps_state_t;

/**
 * Initialise the power supply module.
 * Must be called after ucpd_init().
 */
esp_err_t power_supply_init(void);

/**
 * Set output voltage with automatic PDO mode selection.
 * Tries PPS first for precise control, falls back to Fixed PDO.
 *
 * @param voltage_mv      Target voltage in mV (3300–21000 for PPS)
 * @param min_current_ma  Minimum acceptable current from PDO
 * @return ESP_OK on success, ESP_FAIL if no suitable PDO found
 */
esp_err_t power_supply_set_voltage(uint16_t voltage_mv, uint16_t min_current_ma);

/**
 * Set output voltage in PPS mode (precise, programmable).
 * Voltage is clamped and rounded to 100 mV hardware steps.
 *
 * @param voltage_mv      Target voltage (clamped to PS_PPS_VMIN_MV – PS_PPS_VMAX_MV)
 * @param current_limit_ma Maximum output current (clamped to PS_IMAX_MA)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if PPS unavailable
 */
esp_err_t power_supply_set_pps(uint16_t voltage_mv, uint16_t current_limit_ma);

/**
 * Set output voltage in AVS mode (Extended Power Range).
 * Voltage is clamped and rounded to 200 mV hardware steps.
 *
 * @param voltage_mv      Target voltage (clamped to PS_AVS_VMIN_MV – PS_AVS_VMAX_MV)
 * @param current_limit_ma Maximum output current (clamped to PS_IMAX_MA)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if AVS unavailable
 */
esp_err_t power_supply_set_avs(uint16_t voltage_mv, uint16_t current_limit_ma);

/**
 * Enable or disable the VOUT output switch.
 * @param enable  true = output on, false = output off
 */
esp_err_t power_supply_set_output(bool enable);

/**
 * Step voltage up by one hardware increment.
 * PPS: +100 mV, AVS: +200 mV. No-op for Fixed mode.
 */
esp_err_t power_supply_step_up(void);

/**
 * Step voltage down by one hardware increment.
 * PPS: -100 mV, AVS: -200 mV. No-op for Fixed mode.
 */
esp_err_t power_supply_step_down(void);

/** Get the current power supply state. */
const ps_state_t *power_supply_get_state(void);

/** Validate that a voltage is within PPS range. */
static inline bool ps_is_pps_voltage_valid(uint16_t mv) {
    return mv >= PS_PPS_VMIN_MV && mv <= PS_PPS_VMAX_MV;
}

/** Validate that a voltage is within AVS range. */
static inline bool ps_is_avs_voltage_valid(uint16_t mv) {
    return mv >= PS_AVS_VMIN_MV && mv <= PS_AVS_VMAX_MV;
}

/** Round a voltage to the nearest PPS hardware step (100 mV). */
static inline uint16_t ps_round_pps_voltage(uint16_t mv) {
    return (uint16_t)((mv / PS_PPS_VSTEP_MV) * PS_PPS_VSTEP_MV);
}

/** Round a voltage to the nearest AVS hardware step (200 mV). */
static inline uint16_t ps_round_avs_voltage(uint16_t mv) {
    return (uint16_t)((mv / PS_AVS_VSTEP_MV) * PS_AVS_VSTEP_MV);
}

#endif /* SPARK_POWER_SUPPLY_H */
