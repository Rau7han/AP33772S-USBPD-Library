/*
 * power_supply.c  —  Spark Analyzer Programmable Power Supply
 */

#include "power_supply.h"
#include "ucpd_controller.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PowerSupply";

static ps_state_t s_state = {
    .mode            = PS_MODE_FIXED,
    .voltage_mv      = 5000,
    .current_limit_ma = 1000,
    .pdo_index       = 0,
    .output_enabled  = true,
};

esp_err_t power_supply_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.mode             = PS_MODE_FIXED;
    s_state.voltage_mv       = 5000;
    s_state.current_limit_ma = 1000;
    s_state.output_enabled   = true;
    ESP_LOGI(TAG, "Power supply module initialised");
    return ESP_OK;
}

esp_err_t power_supply_set_voltage(uint16_t voltage_mv, uint16_t min_current_ma) {
    if (min_current_ma > PS_IMAX_MA) min_current_ma = PS_IMAX_MA;
    uint8_t idx = ucpd_set_voltage(voltage_mv, min_current_ma);
    if (idx == 0) {
        ESP_LOGW(TAG, "No PDO found for %u mV / %u mA", voltage_mv, min_current_ma);
        return ESP_FAIL;
    }
    if (ucpd_wait_negotiation(3000) != ESP_OK) {
        ESP_LOGW(TAG, "Negotiation timeout for %u mV", voltage_mv);
        return ESP_FAIL;
    }
    s_state.voltage_mv       = ucpd_get_requested_voltage_mv();
    s_state.current_limit_ma = ucpd_get_requested_current_ma();
    s_state.pdo_index        = idx;
    ESP_LOGI(TAG, "Voltage set: %u mV / %u mA (PDO %u)",
             s_state.voltage_mv, s_state.current_limit_ma, idx);
    return ESP_OK;
}

esp_err_t power_supply_set_pps(uint16_t voltage_mv, uint16_t current_limit_ma) {
    /* Clamp and round to hardware step */
    if (voltage_mv < PS_PPS_VMIN_MV) voltage_mv = PS_PPS_VMIN_MV;
    if (voltage_mv > PS_PPS_VMAX_MV) voltage_mv = PS_PPS_VMAX_MV;
    if (current_limit_ma > PS_IMAX_MA) current_limit_ma = PS_IMAX_MA;
    voltage_mv = ps_round_pps_voltage(voltage_mv);

    uint8_t idx = ucpd_set_pps(voltage_mv, current_limit_ma);
    if (idx == 0) {
        ESP_LOGW(TAG, "PPS not available");
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_state.mode             = PS_MODE_PPS;
    s_state.voltage_mv       = voltage_mv;
    s_state.current_limit_ma = current_limit_ma;
    s_state.pdo_index        = idx;
    ESP_LOGI(TAG, "PPS: %u mV / %u mA (PDO %u)", voltage_mv, current_limit_ma, idx);
    return ESP_OK;
}

esp_err_t power_supply_set_avs(uint16_t voltage_mv, uint16_t current_limit_ma) {
    if (voltage_mv < PS_AVS_VMIN_MV) voltage_mv = PS_AVS_VMIN_MV;
    if (voltage_mv > PS_AVS_VMAX_MV) voltage_mv = PS_AVS_VMAX_MV;
    if (current_limit_ma > PS_IMAX_MA) current_limit_ma = PS_IMAX_MA;
    voltage_mv = ps_round_avs_voltage(voltage_mv);

    uint8_t idx = ucpd_set_avs(voltage_mv, current_limit_ma);
    if (idx == 0) {
        ESP_LOGW(TAG, "AVS not available");
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_state.mode             = PS_MODE_AVS;
    s_state.voltage_mv       = voltage_mv;
    s_state.current_limit_ma = current_limit_ma;
    s_state.pdo_index        = idx;
    ESP_LOGI(TAG, "AVS: %u mV / %u mA (PDO %u)", voltage_mv, current_limit_ma, idx);
    return ESP_OK;
}

esp_err_t power_supply_set_output(bool enable) {
    esp_err_t err = ucpd_set_output(enable);
    if (err == ESP_OK) {
        s_state.output_enabled = enable;
        ESP_LOGI(TAG, "VOUT %s", enable ? "ON" : "OFF");
    }
    return err;
}

esp_err_t power_supply_step_up(void) {
    if (s_state.mode == PS_MODE_PPS) {
        uint16_t next = s_state.voltage_mv + PS_PPS_VSTEP_MV;
        if (next > PS_PPS_VMAX_MV) next = PS_PPS_VMAX_MV;
        return power_supply_set_pps(next, s_state.current_limit_ma);
    } else if (s_state.mode == PS_MODE_AVS) {
        uint16_t next = s_state.voltage_mv + PS_AVS_VSTEP_MV;
        if (next > PS_AVS_VMAX_MV) next = PS_AVS_VMAX_MV;
        return power_supply_set_avs(next, s_state.current_limit_ma);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t power_supply_step_down(void) {
    if (s_state.mode == PS_MODE_PPS) {
        if (s_state.voltage_mv <= PS_PPS_VMIN_MV) return ESP_OK;
        uint16_t next = s_state.voltage_mv - PS_PPS_VSTEP_MV;
        if (next < PS_PPS_VMIN_MV) next = PS_PPS_VMIN_MV;
        return power_supply_set_pps(next, s_state.current_limit_ma);
    } else if (s_state.mode == PS_MODE_AVS) {
        if (s_state.voltage_mv <= PS_AVS_VMIN_MV) return ESP_OK;
        uint16_t next = s_state.voltage_mv - PS_AVS_VSTEP_MV;
        if (next < PS_AVS_VMIN_MV) next = PS_AVS_VMIN_MV;
        return power_supply_set_avs(next, s_state.current_limit_ma);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

const ps_state_t *power_supply_get_state(void) {
    return &s_state;
}
