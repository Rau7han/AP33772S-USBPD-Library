/*
 * config_manager.h / config_manager.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Persistent configuration management for Spark Analyzer.
 * All settings are stored in ESP32 NVS (Non-Volatile Storage).
 *
 * Configuration keys:
 *   spark_cfg namespace:
 *     "wifi_ssid"   — saved Wi-Fi SSID
 *     "wifi_pass"   — saved Wi-Fi password
 *     "def_volt"    — default voltage at startup (mV)
 *     "def_curr"    — default current limit (mA)
 *     "out_on"      — output on at startup (bool)
 *     "ovp_off"     — OVP offset (mV)
 *     "uvp_mode"    — UVP threshold mode (1/2/3)
 *     "otp_thr"     — OTP threshold (°C)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_CONFIG_MANAGER_H
#define SPARK_CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/** Full device configuration */
typedef struct {
    /* Wi-Fi */
    char     wifi_ssid[33];
    char     wifi_pass[65];

    /* Power supply defaults */
    uint16_t default_voltage_mv;    /**< Default voltage at startup (mV), 0=5V */
    uint16_t default_current_ma;    /**< Default current limit (mA) */
    bool     output_on_startup;     /**< Whether VOUT is on after boot */

    /* Protection settings */
    uint16_t ovp_offset_mv;         /**< OVP offset above VREQ */
    uint8_t  uvp_mode;              /**< 1=80%, 2=75%, 3=70% */
    uint8_t  otp_threshold_c;       /**< OTP temperature threshold */
} spark_config_t;

/** Default configuration values */
#define SPARK_CFG_DEFAULT { \
    .wifi_ssid          = "",       \
    .wifi_pass          = "",       \
    .default_voltage_mv = 5000,     \
    .default_current_ma = 1000,     \
    .output_on_startup  = true,     \
    .ovp_offset_mv      = 2000,     \
    .uvp_mode           = 1,        \
    .otp_threshold_c    = 100,      \
}

/**
 * Initialise the config manager and load saved configuration.
 * Must be called after nvs_flash_init().
 */
esp_err_t config_init(void);

/** Save the current configuration to NVS. */
esp_err_t config_save(const spark_config_t *cfg);

/** Load the saved configuration from NVS. */
esp_err_t config_load(spark_config_t *cfg);

/** Reset all configuration to factory defaults and save. */
esp_err_t config_reset(void);

/** Get a pointer to the active (in-memory) configuration. */
const spark_config_t *config_get(void);

/** Update a single string field and save. */
esp_err_t config_set_string(const char *key, const char *value);

/** Update a single integer field and save. */
esp_err_t config_set_int(const char *key, int32_t value);

#endif /* SPARK_CONFIG_MANAGER_H */
