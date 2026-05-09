/*
 * wifi_manager.c  —  Spark Analyzer Wi-Fi Manager (ESP-IDF)
 */

#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "WiFiMgr";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static wifi_state_t       s_state        = WIFI_STATE_DISCONNECTED;
static int                s_retry_count  = 0;
static char               s_ip_str[16]   = "0.0.0.0";
static char               s_ssid[33]     = "";

/* ── NVS helpers ─────────────────────────────────────────────────────────── */
static esp_err_t _load_credentials(char *ssid, char *pass) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("spark_wifi", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t ssid_len = 33, pass_len = 65;
    nvs_get_str(h, "ssid", ssid, &ssid_len);
    nvs_get_str(h, "pass", pass, &pass_len);
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t _save_credentials(const char *ssid, const char *pass) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("spark_wifi", NVS_READWRITE, &h));
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

/* ── Event handler ───────────────────────────────────────────────────────── */
static void _event_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *event_data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < SPARK_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            s_state = WIFI_STATE_CONNECTING;
            ESP_LOGW(TAG, "Reconnecting (%d/%d)...", s_retry_count, SPARK_WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&e->ip_info.ip));
        s_state = WIFI_STATE_CONNECTED;
        s_retry_count = 0;
        ESP_LOGI(TAG, "Connected! IP: %s", s_ip_str);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* ── Start as AP for provisioning ───────────────────────────────────────── */
static void _start_ap(void) {
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, SPARK_AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(SPARK_AP_SSID);
    if (strlen(SPARK_AP_PASSWORD) >= 8) {
        strncpy((char *)ap_cfg.ap.password, SPARK_AP_PASSWORD, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    ap_cfg.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_state = WIFI_STATE_AP_MODE;
    strncpy(s_ssid, SPARK_AP_SSID, sizeof(s_ssid));
    strncpy(s_ip_str, "192.168.4.1", sizeof(s_ip_str));
    ESP_LOGI(TAG, "AP mode: SSID='%s'  IP=192.168.4.1", SPARK_AP_SSID);
}

/* ── Start mDNS ──────────────────────────────────────────────────────────── */
static void _start_mdns(void) {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(SPARK_MDNS_HOSTNAME));
    mdns_instance_name_set("Spark Analyzer");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://%s.local/", SPARK_MDNS_HOSTNAME);
}

/* ── Manager task ────────────────────────────────────────────────────────── */
static void _wifi_task(void *arg) {
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &_event_handler, NULL, &inst_got_ip));

    /* Load saved credentials */
    char ssid[33] = "", pass[65] = "";
    _load_credentials(ssid, pass);

    if (strlen(ssid) > 0) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid,     ssid, sizeof(sta_cfg.sta.ssid));
        strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy(s_ssid, ssid, sizeof(s_ssid));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();

        EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(15000));

        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "STA connection failed — switching to AP mode");
            esp_wifi_stop();
            _start_ap();
        }
    } else {
        ESP_LOGI(TAG, "No credentials saved — starting AP for provisioning");
        _start_ap();
    }

    _start_mdns();

    /* Keep task alive for reconnection monitoring */
    for (;;) {
        if (s_state == WIFI_STATE_CONNECTED && esp_wifi_connect() != ESP_OK) {
            /* Connection lost — will be handled by event handler */
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t wifi_manager_start(void) {
    xTaskCreatePinnedToCore(_wifi_task, "wifi_mgr", 6144, NULL, 5, NULL, 0);
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password) {
    _save_credentials(ssid, password);
    ESP_LOGI(TAG, "Credentials saved for SSID: %s — restarting Wi-Fi", ssid);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid,     ssid,     sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy(s_ssid, ssid, sizeof(s_ssid));
    s_retry_count = 0;
    s_state = WIFI_STATE_CONNECTING;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void)    { return s_state; }
bool         wifi_manager_is_connected(void) { return s_state == WIFI_STATE_CONNECTED; }
const char  *wifi_manager_get_ip(void)       { return s_ip_str; }
const char  *wifi_manager_get_ssid(void)     { return s_ssid; }
int8_t       wifi_manager_get_rssi(void) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) return info.rssi;
    return 0;
}
