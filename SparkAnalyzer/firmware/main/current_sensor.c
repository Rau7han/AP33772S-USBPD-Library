/*
 * current_sensor.c  —  Spark Analyzer Current Sensor Implementation
 */

#include "current_sensor.h"
#include "ucpd_controller.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Sensor";

/* ── INA219 register addresses ───────────────────────────────────────────── */
#define INA219_REG_CONFIG    0x00
#define INA219_REG_SHUNT_V   0x01
#define INA219_REG_BUS_V     0x02
#define INA219_REG_POWER     0x03
#define INA219_REG_CURRENT   0x04
#define INA219_REG_CALIB     0x05

/* INA219 config: 32V bus, ±320mV shunt, 12-bit ADC, continuous */
#define INA219_CONFIG_DEFAULT 0x399F

static bool     s_has_ina219 = false;
static int16_t  s_zero_offset_ma = 0;
static uint16_t s_avg_buf[SENSOR_MOVING_AVG_N];
static uint8_t  s_avg_idx = 0;
static bool     s_avg_full = false;

/* ── INA219 I2C helpers ──────────────────────────────────────────────────── */
static esp_err_t _ina219_write16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_master_write_to_device(UCPD_I2C_PORT, INA219_I2C_ADDR,
                                      buf, 3, pdMS_TO_TICKS(10));
}

static int32_t _ina219_read16(uint8_t reg) {
    uint8_t buf[2] = {0, 0};
    esp_err_t err = i2c_master_write_read_device(UCPD_I2C_PORT, INA219_I2C_ADDR,
                        &reg, 1, buf, 2, pdMS_TO_TICKS(10));
    if (err != ESP_OK) return -1;
    return (int32_t)((int16_t)((buf[0] << 8) | buf[1]));
}

/* ── INA219 initialisation ───────────────────────────────────────────────── */
static bool _ina219_init(void) {
    if (_ina219_write16(INA219_REG_CONFIG, INA219_CONFIG_DEFAULT) != ESP_OK) {
        return false;
    }
    /* Calibration register: Cal = 0.04096 / (current_LSB * Rshunt)
     * With Rshunt = 10mΩ and current_LSB = 1mA: Cal = 4096 */
    _ina219_write16(INA219_REG_CALIB, 4096);
    ESP_LOGI(TAG, "INA219 detected at 0x%02X", INA219_I2C_ADDR);
    return true;
}

static uint16_t _ina219_read_current_ma(void) {
    int32_t raw = _ina219_read16(INA219_REG_CURRENT);
    if (raw < 0) return 0;
    /* With calibration above: 1 LSB = 1 mA */
    int16_t current_ma = (int16_t)raw;
    if (current_ma < 0) current_ma = 0;   /* No negative current in sink mode */
    return (uint16_t)current_ma;
}

static uint16_t _ina219_read_voltage_mv(void) {
    int32_t raw = _ina219_read16(INA219_REG_BUS_V);
    if (raw < 0) return 0;
    /* Bus voltage: bits [15:3], LSB = 4 mV */
    return (uint16_t)(((uint16_t)raw >> 3) * 4u);
}

/* ── Moving average ──────────────────────────────────────────────────────── */
static uint16_t _moving_avg(uint16_t new_val) {
    s_avg_buf[s_avg_idx] = new_val;
    s_avg_idx = (s_avg_idx + 1) % SENSOR_MOVING_AVG_N;
    if (s_avg_idx == 0) s_avg_full = true;
    uint8_t count = s_avg_full ? SENSOR_MOVING_AVG_N : s_avg_idx;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) sum += s_avg_buf[i];
    return (uint16_t)(sum / count);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t current_sensor_init(void) {
    memset(s_avg_buf, 0, sizeof(s_avg_buf));
    s_avg_idx  = 0;
    s_avg_full = false;
    s_zero_offset_ma = 0;

    /* Try to find INA219 on the shared I2C bus */
    s_has_ina219 = _ina219_init();
    if (!s_has_ina219) {
        ESP_LOGI(TAG, "INA219 not found — using AP33772S internal ADC");
    }
    return ESP_OK;
}

uint16_t current_sensor_read_ma(void) {
    uint16_t raw = s_has_ina219
                   ? _ina219_read_current_ma()
                   : ucpd_get_current_ma();
    /* Apply zero-offset correction */
    if ((int32_t)raw - s_zero_offset_ma < 0) return 0;
    return (uint16_t)((int32_t)raw - s_zero_offset_ma);
}

uint16_t current_sensor_read_voltage_mv(void) {
    return s_has_ina219
           ? _ina219_read_voltage_mv()
           : ucpd_get_voltage_mv();
}

esp_err_t current_sensor_read(sensor_reading_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    out->voltage_mv = current_sensor_read_voltage_mv();
    out->current_ma = current_sensor_read_ma();
    out->power_mw   = (uint32_t)out->voltage_mv * out->current_ma / 1000;
    out->shunt_uv   = 0;
    if (s_has_ina219) {
        int32_t sv = _ina219_read16(INA219_REG_SHUNT_V);
        out->shunt_uv = (sv >= 0) ? (uint16_t)(sv * 10u) : 0; /* LSB = 10 µV */
    }
    return ESP_OK;
}

esp_err_t current_sensor_read_averaged(sensor_reading_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    sensor_reading_t instant;
    esp_err_t err = current_sensor_read(&instant);
    if (err != ESP_OK) return err;
    out->current_ma = _moving_avg(instant.current_ma);
    out->voltage_mv = instant.voltage_mv;
    out->power_mw   = (uint32_t)out->voltage_mv * out->current_ma / 1000;
    out->shunt_uv   = instant.shunt_uv;
    return ESP_OK;
}

esp_err_t current_sensor_calibrate(void) {
    /* Average 16 readings to determine zero offset */
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += current_sensor_read_ma();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_zero_offset_ma = (int16_t)(sum / 16);
    ESP_LOGI(TAG, "Zero offset calibration: %d mA", s_zero_offset_ma);
    return ESP_OK;
}

bool current_sensor_has_external(void) {
    return s_has_ina219;
}
