/*
 * config_manager.c  —  Spark Analyzer Configuration Manager (ESP-IDF)
 */

#include "config_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG      = "Config";
static const char *NVS_NS   = "spark_cfg";

static spark_config_t s_cfg = SPARK_CFG_DEFAULT;

/* ── Load from NVS ───────────────────────────────────────────────────────── */
esp_err_t config_load(spark_config_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found — using defaults");
        *cfg = (spark_config_t)SPARK_CFG_DEFAULT;
        return err;
    }

    size_t len;
    len = sizeof(cfg->wifi_ssid);
    nvs_get_str(h, "wifi_ssid", cfg->wifi_ssid, &len);
    len = sizeof(cfg->wifi_pass);
    nvs_get_str(h, "wifi_pass", cfg->wifi_pass, &len);

    uint16_t u16;
    uint8_t  u8;
    uint8_t  b;

    if (nvs_get_u16(h, "def_volt", &u16)  == ESP_OK) cfg->default_voltage_mv   = u16;
    if (nvs_get_u16(h, "def_curr", &u16)  == ESP_OK) cfg->default_current_ma   = u16;
    if (nvs_get_u8 (h, "out_on",   &b)    == ESP_OK) cfg->output_on_startup    = (b != 0);
    if (nvs_get_u16(h, "ovp_off",  &u16)  == ESP_OK) cfg->ovp_offset_mv        = u16;
    if (nvs_get_u8 (h, "uvp_mode", &u8)   == ESP_OK) cfg->uvp_mode             = u8;
    if (nvs_get_u8 (h, "otp_thr",  &u8)   == ESP_OK) cfg->otp_threshold_c      = u8;

    nvs_close(h);
    ESP_LOGI(TAG, "Configuration loaded");
    return ESP_OK;
}

/* ── Save to NVS ─────────────────────────────────────────────────────────── */
esp_err_t config_save(const spark_config_t *cfg) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));

    nvs_set_str(h, "wifi_ssid", cfg->wifi_ssid);
    nvs_set_str(h, "wifi_pass", cfg->wifi_pass);
    nvs_set_u16(h, "def_volt",  cfg->default_voltage_mv);
    nvs_set_u16(h, "def_curr",  cfg->default_current_ma);
    nvs_set_u8 (h, "out_on",    cfg->output_on_startup ? 1 : 0);
    nvs_set_u16(h, "ovp_off",   cfg->ovp_offset_mv);
    nvs_set_u8 (h, "uvp_mode",  cfg->uvp_mode);
    nvs_set_u8 (h, "otp_thr",   cfg->otp_threshold_c);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Configuration saved");
    return ESP_OK;
}

esp_err_t config_init(void) {
    spark_config_t defaults = SPARK_CFG_DEFAULT;
    s_cfg = defaults;
    config_load(&s_cfg);
    return ESP_OK;
}

esp_err_t config_reset(void) {
    s_cfg = (spark_config_t)SPARK_CFG_DEFAULT;
    return config_save(&s_cfg);
}

const spark_config_t *config_get(void) {
    return &s_cfg;
}

esp_err_t config_set_string(const char *key, const char *value) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    esp_err_t err = nvs_set_str(h, key, value);
    nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        /* Reload to keep in-memory copy in sync */
        config_load(&s_cfg);
    }
    return err;
}

esp_err_t config_set_int(const char *key, int32_t value) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    esp_err_t err = nvs_set_i32(h, key, value);
    nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        config_load(&s_cfg);
    }
    return err;
}
