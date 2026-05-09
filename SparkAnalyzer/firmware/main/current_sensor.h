/*
 * current_sensor.h / current_sensor.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Current and voltage measurement via AP33772S internal ADC.
 * Optionally extended with an external INA219/INA226 for higher accuracy.
 *
 * AP33772S internal ADC accuracy:
 *   Voltage : LSB = 80 mV  (sufficient for ~1% accuracy at 5V+)
 *   Current : LSB = 24 mA  (sufficient for coarse measurements)
 *
 * For ±2% accuracy at low currents, use an external INA219 over I2C.
 * Configure CURRENT_SENSOR_USE_INA219 = 1 in sdkconfig to enable.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_CURRENT_SENSOR_H
#define SPARK_CURRENT_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── INA219 I2C address (A0=GND, A1=GND → 0x40) ──────────────────────────── */
#define INA219_I2C_ADDR     0x40
#define INA219_R_SHUNT_mOhm 10u    /* 10 mΩ shunt resistor (adjust to hardware) */

/* ── Measurement averaging ────────────────────────────────────────────────── */
#define SENSOR_MOVING_AVG_N 8      /* Number of samples for moving average */

/** Single measurement snapshot */
typedef struct {
    uint16_t voltage_mv;    /**< Bus voltage in mV */
    uint16_t current_ma;    /**< Current in mA */
    uint32_t power_mw;      /**< Power in mW */
    uint16_t shunt_uv;      /**< Shunt voltage in µV (INA219 only) */
} sensor_reading_t;

/**
 * Initialise the current sensor.
 * If INA219 is not found, falls back to AP33772S internal ADC.
 */
esp_err_t current_sensor_init(void);

/**
 * Read instantaneous current in mA.
 * Uses INA219 if available, otherwise AP33772S ADC.
 */
uint16_t current_sensor_read_ma(void);

/**
 * Read instantaneous bus voltage in mV.
 */
uint16_t current_sensor_read_voltage_mv(void);

/**
 * Read a complete measurement snapshot.
 */
esp_err_t current_sensor_read(sensor_reading_t *out);

/**
 * Read a moving-averaged measurement (SENSOR_MOVING_AVG_N samples).
 * More stable than instantaneous reading for display purposes.
 */
esp_err_t current_sensor_read_averaged(sensor_reading_t *out);

/**
 * Calibrate the current sensor.
 * Applies a zero-current offset correction.
 * Call with load disconnected.
 */
esp_err_t current_sensor_calibrate(void);

/** Returns true if an external INA219/INA226 was detected. */
bool current_sensor_has_external(void);

#endif /* SPARK_CURRENT_SENSOR_H */
