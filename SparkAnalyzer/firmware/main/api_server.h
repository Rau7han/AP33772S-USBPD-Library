/*
 * api_server.h / api_server.c
 * ─────────────────────────────────────────────────────────────────────────────
 * HTTP REST API server for Spark Analyzer (ESP-IDF, esp_http_server).
 *
 * Endpoints:
 *   GET  /               — Live web dashboard (HTML)
 *   GET  /api/status     — Measurements & connection info (JSON)
 *   GET  /api/pdos       — All discovered PDOs (JSON)
 *   POST /api/voltage    — Set voltage {"voltage_mv":N,"current_ma":N}
 *   POST /api/pps        — PPS request   {"voltage_mv":N,"current_ma":N}
 *   POST /api/avs        — AVS request   {"voltage_mv":N,"current_ma":N}
 *   POST /api/output     — VOUT control  {"on":true|false}
 *   POST /api/reset      — PD hard reset
 *   POST /api/wifi       — Wi-Fi config  {"ssid":"...","pass":"..."}
 *
 * All POST requests: Content-Type: application/json
 * All responses:     application/json (except GET / which returns text/html)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_API_SERVER_H
#define SPARK_API_SERVER_H

#include "ble_manager.h"   /* for spark_measurements_t */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"

/**
 * Start the HTTP REST API server.
 * @param meas      Pointer to shared measurement struct (read by API handlers)
 * @param meas_mutex Mutex protecting the measurement struct
 */
esp_err_t api_server_start(spark_measurements_t *meas,
                           SemaphoreHandle_t meas_mutex);

/** Stop the HTTP server. */
esp_err_t api_server_stop(void);

#endif /* SPARK_API_SERVER_H */
