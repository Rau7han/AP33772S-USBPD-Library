/*
 * ucpd_controller.h / ucpd_controller.c
 * ─────────────────────────────────────────────────────────────────────────────
 * USB-C Power Delivery controller abstraction for the Spark Analyzer.
 * Wraps the AP33772S I2C register interface exposed via the Arduino library
 * conventions, re-implemented for bare-metal ESP-IDF.
 *
 * Supports:
 *   - Fixed PDO negotiation (5 V / 9 V / 12 V / 15 V / 20 V)
 *   - PPS (Programmable Power Supply): 3.3 V – 21 V in 100 mV steps
 *   - AVS (Adjustable Voltage Supply): 15 V – 28 V in 200 mV steps
 *   - Interrupt-driven fault detection
 *   - PPS/AVS 500 ms keep-alive
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_UCPD_CONTROLLER_H
#define SPARK_UCPD_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── AP33772S I2C configuration ────────────────────────────────────────────── */
#define UCPD_I2C_PORT       I2C_NUM_0
#define UCPD_I2C_ADDR       0x52
#define UCPD_I2C_SDA_GPIO   21
#define UCPD_I2C_SCL_GPIO   22
#define UCPD_I2C_FREQ_HZ    400000
#define UCPD_INT_GPIO       19      /* Set to -1 to disable interrupt */

/* ── PPS/AVS hardware limits ───────────────────────────────────────────────── */
#define UCPD_PPS_VMIN_MV    3300u
#define UCPD_PPS_VMAX_MV    21000u
#define UCPD_PPS_VSTEP_MV   100u
#define UCPD_AVS_VMIN_MV    15000u
#define UCPD_AVS_VMAX_MV    28000u
#define UCPD_AVS_VSTEP_MV   200u
#define UCPD_MAX_PDOS       13u

/* ── State machine states ──────────────────────────────────────────────────── */
typedef enum {
    UCPD_STATE_IDLE     = 0,
    UCPD_STATE_STARTUP  = 1,
    UCPD_STATE_SCAN     = 2,
    UCPD_STATE_RUNNING  = 3,
    UCPD_STATE_FAULT    = 4,
    UCPD_STATE_RECOVERY = 5,
} ucpd_state_t;

/* ── PDO descriptor ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  index;         /* 1-based (1–13) */
    bool     valid;         /* Slot populated */
    bool     is_epr;        /* EPR (index 8–13) */
    uint8_t  type;          /* 0=Fixed, 1=PPS, 2=AVS */
    uint16_t min_mv;        /* Minimum voltage (0 for Fixed) */
    uint16_t max_mv;        /* Maximum voltage */
    uint16_t max_ma;        /* Maximum current */
} ucpd_pdo_t;

/* ── Public API ────────────────────────────────────────────────────────────── */

/** Initialise I2C bus and AP33772S. Must be called before any other function. */
esp_err_t ucpd_init(void);

/**
 * Run one iteration of the PD state machine.
 * @param current_state  Current state
 * @return               Next state (may be same as current)
 */
ucpd_state_t ucpd_run(ucpd_state_t current_state);

/**
 * Re-send PPS/AVS RDO keep-alive if needed.
 * Call every loop iteration (safe to call even when not in PPS/AVS mode).
 */
void ucpd_keepalive(void);

/** Return a human-readable state name. */
const char *ucpd_state_name(ucpd_state_t state);

/** Read all PDOs from the AP33772S. Returns count of valid PDOs. */
uint8_t ucpd_read_all_pdos(void);

/** Get a decoded PDO by 1-based index. Returns NULL if invalid. */
const ucpd_pdo_t *ucpd_get_pdo(uint8_t index);

/** Return count of valid PDOs. */
uint8_t ucpd_get_pdo_count(void);

/**
 * Auto-select the best PDO and request a voltage.
 * Prefers PPS/AVS for precise control, falls back to nearest Fixed PDO.
 * @param voltage_mv     Target voltage in mV
 * @param min_current_ma Minimum acceptable PDO current in mA
 * @return Selected PDO index (1–13), or 0 on failure
 */
uint8_t ucpd_set_voltage(uint16_t voltage_mv, uint16_t min_current_ma);

/**
 * Request PPS at exact voltage.
 * @return Selected PDO index, or 0 if PPS unavailable
 */
uint8_t ucpd_set_pps(uint16_t voltage_mv, uint16_t max_current_ma);

/**
 * Request AVS at exact voltage.
 * @return Selected PDO index, or 0 if AVS unavailable
 */
uint8_t ucpd_set_avs(uint16_t voltage_mv, uint16_t max_current_ma);

/** Convenience fixed-voltage wrappers. */
uint8_t ucpd_set_5v (uint16_t min_ma);
uint8_t ucpd_set_9v (uint16_t min_ma);
uint8_t ucpd_set_12v(uint16_t min_ma);
uint8_t ucpd_set_15v(uint16_t min_ma);
uint8_t ucpd_set_20v(uint16_t min_ma);

/** Control the VOUT switch (AP33772S SYSTEM register). */
esp_err_t ucpd_set_output(bool on);

/** Issue a PD hard reset. */
esp_err_t ucpd_hard_reset(void);

/** Wait for PD negotiation to complete. */
esp_err_t ucpd_wait_negotiation(uint32_t timeout_ms);

/* ── ADC / measurement readings ────────────────────────────────────────────── */
uint16_t ucpd_get_voltage_mv(void);
uint16_t ucpd_get_current_ma(void);
uint32_t ucpd_get_power_mw(void);
int8_t   ucpd_get_temperature_c(void);
uint16_t ucpd_get_requested_voltage_mv(void);
uint16_t ucpd_get_requested_current_ma(void);

/* ── Status queries ────────────────────────────────────────────────────────── */
bool    ucpd_is_pd_connected(void);
bool    ucpd_is_legacy_connected(void);
bool    ucpd_is_cable_flipped(void);
bool    ucpd_is_derating(void);
bool    ucpd_is_fault(void);
uint8_t ucpd_get_fault_flags(void);    /* Bitmask of STATUS_* flags */

/* ── Protection configuration ──────────────────────────────────────────────── */
esp_err_t ucpd_set_ovp_offset_mv(uint16_t offset_mv);
esp_err_t ucpd_set_uvp_threshold(uint8_t mode);   /* 1=80%, 2=75%, 3=70% */
esp_err_t ucpd_set_ocp_threshold_ma(uint16_t ma); /* 0 = auto (110% PDO max) */
esp_err_t ucpd_set_otp_threshold_c(uint8_t deg_c);

#endif /* SPARK_UCPD_CONTROLLER_H */
