/*
 * main.c  —  Spark Analyzer ESP-IDF Firmware Entry Point
 * ─────────────────────────────────────────────────────────────────────────────
 * ESP32-powered USB-C PD Analyzer & Programmable Power Supply
 *
 * Architecture:
 *   app_main() initialises all subsystems and starts the FreeRTOS tasks:
 *     - ucpd_task     : USB-C PD negotiation & PPS keep-alive (core 1)
 *     - wifi_task     : Wi-Fi provisioning, STA connection, mDNS (core 0)
 *     - api_task      : HTTP REST API server (core 0)
 *     - ble_task      : BLE GATT server for mobile app (core 0)
 *     - sensor_task   : Current/voltage ADC sampling (core 1)
 *     - monitor_task  : Periodic status log & data recording (core 0)
 *
 * Build: ESP-IDF 5.x  (idf.py build flash monitor)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/i2c.h"

#include "ucpd_controller.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "api_server.h"
#include "power_supply.h"
#include "current_sensor.h"
#include "config_manager.h"

static const char *TAG = "SparkAnalyzer";

/* ── Shared measurement data (protected by mutex) ─────────────────────────── */
static spark_measurements_t s_meas = {0};
static SemaphoreHandle_t    s_meas_mutex;

/* ── Task handles ─────────────────────────────────────────────────────────── */
static TaskHandle_t h_ucpd    = NULL;
static TaskHandle_t h_sensor  = NULL;
static TaskHandle_t h_monitor = NULL;

/* ─────────────────────────────────────────────────────────────────────────── */
/*  UCPD Task — PD negotiation + PPS keep-alive                               */
/* ─────────────────────────────────────────────────────────────────────────── */
static void ucpd_task(void *arg)
{
    ESP_LOGI(TAG, "UCPD task started");
    ucpd_init();

    ucpd_state_t pd_state = UCPD_STATE_STARTUP;

    for (;;) {
        ucpd_state_t next = ucpd_run(pd_state);
        if (next != pd_state) {
            ESP_LOGI(TAG, "PD state: %s → %s",
                     ucpd_state_name(pd_state), ucpd_state_name(next));
            pd_state = next;
        }
        /* PPS keep-alive: re-send RDO every 500 ms */
        ucpd_keepalive();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Sensor Task — ADC sampling of voltage & current                           */
/* ─────────────────────────────────────────────────────────────────────────── */
static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");
    current_sensor_init();

    for (;;) {
        spark_measurements_t m;
        m.voltage_mv  = ucpd_get_voltage_mv();
        m.current_ma  = current_sensor_read_ma();
        m.power_mw    = (uint32_t)m.voltage_mv * m.current_ma / 1000;
        m.temp_c      = ucpd_get_temperature_c();
        m.vreq_mv     = ucpd_get_requested_voltage_mv();
        m.ireq_ma     = ucpd_get_requested_current_ma();

        if (xSemaphoreTake(s_meas_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_meas = m;
            xSemaphoreGive(s_meas_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(50));   /* 20 Hz sampling */
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Monitor Task — periodic status log                                        */
/* ─────────────────────────────────────────────────────────────────────────── */
static void monitor_task(void *arg)
{
    for (;;) {
        spark_measurements_t m;
        if (xSemaphoreTake(s_meas_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            m = s_meas;
            xSemaphoreGive(s_meas_mutex);
        }
        ESP_LOGI(TAG, "V=%5umV  I=%4umA  P=%6lumW  T=%3d°C",
                 m.voltage_mv, m.current_ma,
                 (unsigned long)m.power_mw, m.temp_c);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  app_main                                                                  */
/* ─────────────────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════╗");
    ESP_LOGI(TAG, "║    Spark Analyzer v1.0       ║");
    ESP_LOGI(TAG, "║  ESP32 + AP33772S USB-C PD   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════╝");

    /* NVS — required by Wi-Fi and config manager */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Shared mutex */
    s_meas_mutex = xSemaphoreCreateMutex();
    configASSERT(s_meas_mutex);

    /* Load persisted configuration */
    config_init();

    /* Start subsystem tasks */
    xTaskCreatePinnedToCore(ucpd_task,    "ucpd",    4096, NULL, 5, &h_ucpd,    1);
    xTaskCreatePinnedToCore(sensor_task,  "sensor",  3072, NULL, 4, &h_sensor,  1);

    /* Wi-Fi, BLE, and API run on core 0 alongside the TCP/IP stack */
    wifi_manager_start();   /* Internally creates its own task on core 0 */
    ble_manager_start();    /* Internally creates its own task on core 0 */
    api_server_start(&s_meas, s_meas_mutex);  /* HTTP task on core 0 */

    xTaskCreatePinnedToCore(monitor_task, "monitor", 2048, NULL, 2, &h_monitor, 0);

    ESP_LOGI(TAG, "All tasks started — running");
}
