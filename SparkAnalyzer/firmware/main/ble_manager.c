/*
 * ble_manager.c  —  Spark Analyzer BLE GATT Server (ESP-IDF / NimBLE)
 */

#include "ble_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "BLEMgr";

/* ── State ──────────────────────────────────────────────────────────────── */
static uint16_t           s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static SemaphoreHandle_t  s_mutex;
static spark_measurements_t s_meas      = {0};
static ble_command_t        s_cmd       = {0};
static bool                 s_new_meas  = false;

/* ── Characteristic value handles (set during registration) ─────────────── */
static uint16_t h_voltage  = 0, h_current = 0;
static uint16_t h_power    = 0, h_temp    = 0;
static uint16_t h_status   = 0;

/* ── Custom service / characteristic UUIDs ──────────────────────────────── */
static const ble_uuid128_t svc_ctrl_uuid = BLE_UUID128_INIT(
    0x4b,0x91,0x31,0xc3,0xc9,0xc5,0xcc,0x8f,
    0x9e,0x45,0xb5,0x1f,0x01,0xc2,0xaf,0x4f);

static const ble_uuid128_t chr_volt_set_uuid = BLE_UUID128_INIT(
    0xa8,0x26,0x1b,0x61,0xf5,0xb7,0x88,0x46,
    0xe1,0x36,0x3e,0x48,0x3e,0xb5,0xb5,0xbe);

static const ble_uuid128_t chr_curr_set_uuid = BLE_UUID128_INIT(
    0xa8,0x26,0x1b,0x61,0xf5,0xb7,0x88,0x46,
    0xe1,0x36,0x3e,0x48,0x3f,0xb5,0xb5,0xbe);

static const ble_uuid128_t chr_out_ctrl_uuid = BLE_UUID128_INIT(
    0xa8,0x26,0x1b,0x61,0xf5,0xb7,0x88,0x46,
    0xe1,0x36,0x3e,0x48,0x40,0xb5,0xb5,0xbe);

static const ble_uuid128_t chr_status_uuid = BLE_UUID128_INIT(
    0xa8,0x26,0x1b,0x61,0xf5,0xb7,0x88,0x46,
    0xe1,0x36,0x3e,0x48,0x41,0xb5,0xb5,0xbe);

/* ── Characteristic access callbacks ────────────────────────────────────── */

static int _chr_access_meas(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    uint16_t handle = attr;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    spark_measurements_t m = s_meas;
    xSemaphoreGive(s_mutex);

    if (handle == h_voltage) os_mbuf_append(ctxt->om, &m.voltage_mv, 2);
    else if (handle == h_current) os_mbuf_append(ctxt->om, &m.current_ma, 2);
    else if (handle == h_power)   os_mbuf_append(ctxt->om, &m.power_mw, 4);
    else if (handle == h_temp)    os_mbuf_append(ctxt->om, &m.temp_c, 1);
    return 0;
}

static int _chr_access_volt_set(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t mv = 0;
        os_mbuf_copydata(ctxt->om, 0, 2, &mv);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_cmd.voltage_mv      = mv;
        s_cmd.voltage_pending = true;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "BLE voltage request: %u mV", mv);
    }
    return 0;
}

static int _chr_access_curr_set(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t ma = 0;
        os_mbuf_copydata(ctxt->om, 0, 2, &ma);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_cmd.current_ma      = ma;
        s_cmd.current_pending = true;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "BLE current limit: %u mA", ma);
    }
    return 0;
}

static int _chr_access_out_ctrl(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t on = 0;
        os_mbuf_copydata(ctxt->om, 0, 1, &on);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_cmd.output_on      = (on != 0);
        s_cmd.output_pending = true;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "BLE output: %s", on ? "ON" : "OFF");
    }
    return 0;
}

static int _chr_access_status(uint16_t conn, uint16_t attr,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    spark_measurements_t m = s_meas;
    xSemaphoreGive(s_mutex);
    char json[128];
    snprintf(json, sizeof(json),
             "{\"v\":%u,\"i\":%u,\"p\":%lu,\"t\":%d}",
             m.voltage_mv, m.current_ma, (unsigned long)m.power_mw, m.temp_c);
    os_mbuf_append(ctxt->om, json, strlen(json));
    return 0;
}

/* ── GATT service table ──────────────────────────────────────────────────── */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    /* Measurement service */
    { .type = BLE_GATT_SVC_TYPE_PRIMARY,
      .uuid = BLE_UUID16_DECLARE(0x181A),
      .characteristics = (struct ble_gatt_chr_def[]) {
        { .uuid = BLE_UUID16_DECLARE(0x2B18),  /* Voltage */
          .access_cb = _chr_access_meas,
          .val_handle = &h_voltage,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
        { .uuid = BLE_UUID16_DECLARE(0x2AEE),  /* Current */
          .access_cb = _chr_access_meas,
          .val_handle = &h_current,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
        { .uuid = BLE_UUID16_DECLARE(0x2B05),  /* Power */
          .access_cb = _chr_access_meas,
          .val_handle = &h_power,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
        { .uuid = BLE_UUID16_DECLARE(0x2A6E),  /* Temperature */
          .access_cb = _chr_access_meas,
          .val_handle = &h_temp,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
        { 0 }   /* sentinel */
      }
    },
    /* Control service */
    { .type = BLE_GATT_SVC_TYPE_PRIMARY,
      .uuid = &svc_ctrl_uuid.u,
      .characteristics = (struct ble_gatt_chr_def[]) {
        { .uuid = &chr_volt_set_uuid.u,
          .access_cb = _chr_access_volt_set,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
        { .uuid = &chr_curr_set_uuid.u,
          .access_cb = _chr_access_curr_set,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
        { .uuid = &chr_out_ctrl_uuid.u,
          .access_cb = _chr_access_out_ctrl,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
        { .uuid = &chr_status_uuid.u,
          .access_cb = _chr_access_status,
          .val_handle = &h_status,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
        { 0 }   /* sentinel */
      }
    },
    { 0 }   /* sentinel */
};

/* ── GAP event handler ───────────────────────────────────────────────────── */
static int _gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Client connected (handle=%u)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connect failed — restarting adv");
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Client disconnected — restarting adv");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &(struct ble_gap_adv_params){
                              .conn_mode = BLE_GAP_CONN_MODE_UND,
                              .disc_mode = BLE_GAP_DISC_MODE_GEN,
                          }, _gap_event, NULL);
        break;
    default:
        break;
    }
    return 0;
}

/* ── NimBLE host sync callback ───────────────────────────────────────────── */
static void _on_sync(void) {
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &(struct ble_gap_adv_params){
                          .conn_mode = BLE_GAP_CONN_MODE_UND,
                          .disc_mode = BLE_GAP_DISC_MODE_GEN,
                      }, _gap_event, NULL);
    ESP_LOGI(TAG, "BLE advertising started");
}

/* ── NimBLE host task ─────────────────────────────────────────────────────── */
static void _nimble_host_task(void *param) {
    nimble_port_run();   /* Blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t ble_manager_start(void) {
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return ret;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("SparkAnalyzer");

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    ble_hs_cfg.sync_cb = _on_sync;

    nimble_port_freertos_init(_nimble_host_task);
    ESP_LOGI(TAG, "BLE manager started");
    return ESP_OK;
}

void ble_manager_update(const spark_measurements_t *meas) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_meas    = *meas;
    s_new_meas = true;
    xSemaphoreGive(s_mutex);

    /* Send notifications if client is connected */
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om;
    om = ble_hs_mbuf_from_flat(&meas->voltage_mv, 2);
    ble_gattc_notify_custom(s_conn_handle, h_voltage, om);
    om = ble_hs_mbuf_from_flat(&meas->current_ma, 2);
    ble_gattc_notify_custom(s_conn_handle, h_current, om);
    om = ble_hs_mbuf_from_flat(&meas->power_mw, 4);
    ble_gattc_notify_custom(s_conn_handle, h_power, om);
    om = ble_hs_mbuf_from_flat(&meas->temp_c, 1);
    ble_gattc_notify_custom(s_conn_handle, h_temp, om);
}

bool ble_manager_get_command(ble_command_t *cmd) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool any = s_cmd.voltage_pending || s_cmd.current_pending || s_cmd.output_pending;
    if (any) {
        *cmd = s_cmd;
        memset(&s_cmd, 0, sizeof(s_cmd));
    }
    xSemaphoreGive(s_mutex);
    return any;
}

bool ble_manager_is_connected(void) {
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
