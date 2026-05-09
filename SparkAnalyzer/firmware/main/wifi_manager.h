/*
 * wifi_manager.h / wifi_manager.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Wi-Fi connection management for Spark Analyzer (ESP-IDF).
 *
 * Features:
 *   - Station (STA) mode connection to saved credentials
 *   - Access Point (AP) fallback for first-run provisioning
 *   - mDNS registration (http://spark.local/)
 *   - Automatic reconnection with exponential back-off
 *   - Credentials persisted in NVS
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_WIFI_MANAGER_H
#define SPARK_WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ── Configuration defaults ───────────────────────────────────────────────── */
#define SPARK_AP_SSID          "SparkAnalyzer"
#define SPARK_AP_PASSWORD      "spark1234"    /* Must be 8+ chars or "" for open */
#define SPARK_MDNS_HOSTNAME    "spark"        /* http://spark.local/ */
#define SPARK_WIFI_MAX_RETRY   5              /* Retries before AP fallback */

/** Wi-Fi connection state */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING   = 1,
    WIFI_STATE_CONNECTED    = 2,
    WIFI_STATE_AP_MODE      = 3,
} wifi_state_t;

/**
 * Initialise Wi-Fi and start the manager task.
 * Loads credentials from NVS; falls back to AP mode if none saved.
 */
esp_err_t wifi_manager_start(void);

/**
 * Save new Wi-Fi credentials and trigger reconnection.
 * @param ssid      Network SSID (max 32 chars)
 * @param password  Password (max 64 chars, or "" for open network)
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/** Returns current Wi-Fi connection state. */
wifi_state_t wifi_manager_get_state(void);

/** Returns true if connected to a Wi-Fi network in STA mode. */
bool wifi_manager_is_connected(void);

/** Returns the current IP address as a dotted-decimal string. */
const char *wifi_manager_get_ip(void);

/** Returns the current SSID (or AP SSID in AP mode). */
const char *wifi_manager_get_ssid(void);

/** Returns RSSI in dBm, or 0 if not connected. */
int8_t wifi_manager_get_rssi(void);

#endif /* SPARK_WIFI_MANAGER_H */
