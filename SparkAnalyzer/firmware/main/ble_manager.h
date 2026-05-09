/*
 * ble_manager.h / ble_manager.c
 * ─────────────────────────────────────────────────────────────────────────────
 * BLE GATT server for Spark Analyzer mobile app connectivity (ESP-IDF).
 *
 * Services:
 *   Power Measurement Service (UUID: 0x181A)
 *     - Voltage Characteristic   (UUID: 0x2B18, notify) — uint16_t mV
 *     - Current Characteristic   (UUID: 0x2AEE, notify) — uint16_t mA
 *     - Power Characteristic     (UUID: 0x2B05, notify) — uint32_t mW
 *     - Temperature Characteristic (UUID: 0x2A6E, notify) — int8_t °C
 *
 *   Power Control Service (custom 128-bit UUID)
 *     - Voltage Set (write)  — uint16_t mV
 *     - Current Set (write)  — uint16_t mA
 *     - Output Control (write) — uint8_t 0=off 1=on
 *     - Status JSON (notify)  — string
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_BLE_MANAGER_H
#define SPARK_BLE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/** Shared measurement data passed to BLE notification task. */
typedef struct {
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint32_t power_mw;
    int8_t   temp_c;
    uint16_t vreq_mv;
    uint16_t ireq_ma;
} spark_measurements_t;

/** Pending command from BLE client. */
typedef struct {
    bool     voltage_pending;
    uint16_t voltage_mv;
    bool     current_pending;
    uint16_t current_ma;
    bool     output_pending;
    bool     output_on;
} ble_command_t;

/**
 * Start BLE GATT server in a background task.
 * Call once from app_main().
 */
esp_err_t ble_manager_start(void);

/**
 * Push updated measurements to connected BLE client (notify).
 * Thread-safe — can be called from any task.
 */
void ble_manager_update(const spark_measurements_t *meas);

/**
 * Retrieve the latest pending command from a BLE client write.
 * Resets the pending flags after reading.
 * @return true if any command was pending.
 */
bool ble_manager_get_command(ble_command_t *cmd);

/** Returns true if a BLE client is currently connected. */
bool ble_manager_is_connected(void);

#endif /* SPARK_BLE_MANAGER_H */
