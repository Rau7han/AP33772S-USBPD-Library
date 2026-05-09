/*
 * ucpd_controller.c  —  Spark Analyzer USB-C PD Controller Implementation
 * ─────────────────────────────────────────────────────────────────────────────
 * Drives the AP33772S over I2C (ESP-IDF i2c driver).
 * Register map, PDO layout, and RDO encoding follow DS46176 Rev.9-2.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "ucpd_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UCPD";

/* ── AP33772S register addresses ─────────────────────────────────────────── */
#define REG_STATUS    0x01
#define REG_MASK      0x02
#define REG_OPMODE    0x03
#define REG_CONFIG    0x04
#define REG_PDCONFIG  0x05
#define REG_SYSTEM    0x06
#define REG_VOLTAGE   0x11   /* VOUT, LSB = 80 mV  (2-byte LE) */
#define REG_CURRENT   0x12   /* IOUT, LSB = 24 mA  (1-byte) */
#define REG_TEMP      0x13   /* NTC temp, °C */
#define REG_VREQ      0x14   /* Negotiated voltage, LSB = 50 mV */
#define REG_IREQ      0x15   /* Negotiated current, LSB = 10 mA */
#define REG_UVPTHR    0x17
#define REG_OVPTHR    0x18
#define REG_OCPTHR    0x19
#define REG_OTPTHR    0x1A
#define REG_SRCPDO    0x20   /* 26-byte burst: all 13 PDOs (2 bytes each) */
#define REG_PD_REQMSG 0x31
#define REG_PD_CMDMSG 0x32
#define REG_PD_MSGRLT 0x33

/* ── STATUS bits ──────────────────────────────────────────────────────────── */
#define STATUS_STARTED 0x01
#define STATUS_READY   0x02
#define STATUS_NEWPDO  0x04
#define STATUS_UVP     0x08
#define STATUS_OVP     0x10
#define STATUS_OCP     0x20
#define STATUS_OTP     0x40
#define STATUS_FAULTS  (STATUS_UVP | STATUS_OVP | STATUS_OCP | STATUS_OTP)

/* ── OPMODE bits ─────────────────────────────────────────────────────────── */
#define OPMODE_LGCYMOD 0x01
#define OPMODE_PDMOD   0x02
#define OPMODE_DR      0x40
#define OPMODE_CCFLIP  0x80

/* ── PDCONFIG bits ───────────────────────────────────────────────────────── */
#define PDCFG_EPR_EN   0x01
#define PDCFG_PPS_EN   0x02

/* ── SYSTEM register values ──────────────────────────────────────────────── */
#define SYSTEM_OUTPUT_OFF 0x11
#define SYSTEM_OUTPUT_ON  0x12

/* ── MSGRLT bits ─────────────────────────────────────────────────────────── */
#define MSGRLT_SUCCESS 0x01

/* ── Current map (code 0x0–0xF → 1.0 A – 5.0 A, 250 mA/step) ────────────── */
static const uint16_t s_current_map[16] = {
    1000,1250,1500,1750,2000,2250,2500,2750,
    3000,3250,3500,3750,4000,4250,4500,5000
};

/* ── Internal state ──────────────────────────────────────────────────────── */
static ucpd_pdo_t s_pdos[UCPD_MAX_PDOS];
static uint8_t    s_pdo_count = 0;
static bool       s_ka_active = false;
static uint32_t   s_ka_timer  = 0;
static uint8_t    s_ka_pdo    = 0;
static uint8_t    s_ka_vsел   = 0;
static uint8_t    s_ka_isel   = 0;
static bool       s_ka_is_avs = false;

/* ── I2C helpers ──────────────────────────────────────────────────────────── */
static esp_err_t _write8(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(UCPD_I2C_PORT, UCPD_I2C_ADDR,
                                      buf, 2, pdMS_TO_TICKS(10));
}

static esp_err_t _write16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    return i2c_master_write_to_device(UCPD_I2C_PORT, UCPD_I2C_ADDR,
                                      buf, 3, pdMS_TO_TICKS(10));
}

static int16_t _read8(uint8_t reg) {
    uint8_t val = 0;
    esp_err_t err = i2c_master_write_read_device(UCPD_I2C_PORT, UCPD_I2C_ADDR,
                        &reg, 1, &val, 1, pdMS_TO_TICKS(10));
    return (err == ESP_OK) ? val : -1;
}

static int32_t _read16(uint8_t reg) {
    uint8_t buf[2] = {0, 0};
    esp_err_t err = i2c_master_write_read_device(UCPD_I2C_PORT, UCPD_I2C_ADDR,
                        &reg, 1, buf, 2, pdMS_TO_TICKS(10));
    return (err == ESP_OK) ? (int32_t)(buf[0] | ((uint16_t)buf[1] << 8)) : -1;
}

static esp_err_t _read_burst(uint8_t reg, uint8_t *buf, uint8_t len) {
    return i2c_master_write_read_device(UCPD_I2C_PORT, UCPD_I2C_ADDR,
                                        &reg, 1, buf, len, pdMS_TO_TICKS(20));
}

/* ── Current encode/decode ───────────────────────────────────────────────── */
static uint8_t _current_encode(uint16_t ma) {
    for (int i = 15; i >= 0; i--) {
        if (ma >= s_current_map[i]) return (uint8_t)i;
    }
    return 0;
}

/* ── Send RDO ─────────────────────────────────────────────────────────────── */
static void _send_rdo(uint8_t pdo_index, uint8_t current_sel, uint8_t voltage_sel) {
    uint16_t rdo = (uint16_t)(
        ((pdo_index   & 0x0F) << 12) |
        ((current_sel & 0x0F) <<  8) |
        (voltage_sel  & 0xFF)
    );
    _write16(REG_PD_REQMSG, rdo);

    /* Store keep-alive params */
    s_ka_pdo    = pdo_index;
    s_ka_vsел   = voltage_sel;
    s_ka_isel   = current_sel;
    s_ka_active = true;
    s_ka_timer  = xTaskGetTickCount();
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Public API Implementation                                                 */
/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t ucpd_init(void) {
    /* Configure I2C master */
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = UCPD_I2C_SDA_GPIO,
        .scl_io_num       = UCPD_I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = UCPD_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(UCPD_I2C_PORT, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(UCPD_I2C_PORT, cfg.mode, 0, 0, 0));

    /* Verify AP33772S is present */
    int16_t status = _read8(REG_STATUS);
    if (status < 0) {
        ESP_LOGE(TAG, "AP33772S not found at 0x%02X!", UCPD_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    /* Enable EPR and PPS */
    _write8(REG_PDCONFIG, PDCFG_EPR_EN | PDCFG_PPS_EN);

    /* Enable all protections */
    _write8(REG_CONFIG, 0xF8);

    /* Default protection thresholds */
    ucpd_set_ovp_offset_mv(2000);
    ucpd_set_uvp_threshold(1);   /* 80% */
    ucpd_set_ocp_threshold_ma(0);
    ucpd_set_otp_threshold_c(100);

    /* Unmask all interrupts */
    _write8(REG_MASK, 0x7F);

    ESP_LOGI(TAG, "AP33772S initialised");
    return ESP_OK;
}

ucpd_state_t ucpd_run(ucpd_state_t current_state) {
    switch (current_state) {
    case UCPD_STATE_IDLE:
        return UCPD_STATE_STARTUP;

    case UCPD_STATE_STARTUP: {
        int16_t st = _read8(REG_STATUS);
        if (st >= 0 && (st & STATUS_STARTED)) {
            return UCPD_STATE_SCAN;
        }
        return UCPD_STATE_STARTUP;
    }

    case UCPD_STATE_SCAN: {
        int16_t st = _read8(REG_STATUS);
        if (st >= 0 && (st & STATUS_READY)) {
            uint8_t n = ucpd_read_all_pdos();
            ESP_LOGI(TAG, "%u PDO(s) discovered", n);
            /* Auto-negotiate: 20V → 12V → 5V */
            uint8_t idx = ucpd_set_voltage(20000, 1500);
            if (!idx) idx = ucpd_set_voltage(12000, 1000);
            if (!idx) idx = ucpd_set_5v(500);
            if (idx && ucpd_wait_negotiation(3000) == ESP_OK) {
                return UCPD_STATE_RUNNING;
            }
        }
        return UCPD_STATE_SCAN;
    }

    case UCPD_STATE_RUNNING: {
        int16_t st = _read8(REG_STATUS);
        if (st < 0) return UCPD_STATE_RUNNING;
        if (st & STATUS_FAULTS) {
            ESP_LOGW(TAG, "Fault detected: 0x%02X", st & STATUS_FAULTS);
            return UCPD_STATE_FAULT;
        }
        if (st & STATUS_NEWPDO) {
            ESP_LOGI(TAG, "New PDOs — re-scanning");
            return UCPD_STATE_SCAN;
        }
        return UCPD_STATE_RUNNING;
    }

    case UCPD_STATE_FAULT:
        vTaskDelay(pdMS_TO_TICKS(3000));
        return UCPD_STATE_RECOVERY;

    case UCPD_STATE_RECOVERY: {
        uint16_t target = ucpd_get_requested_voltage_mv();
        if (target == 0) target = 5000;
        uint8_t idx = ucpd_set_voltage(target, 1000);
        if (idx && ucpd_wait_negotiation(3000) == ESP_OK) {
            ESP_LOGI(TAG, "Recovery OK");
            return UCPD_STATE_RUNNING;
        }
        ucpd_hard_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
        return UCPD_STATE_STARTUP;
    }
    }
    return current_state;
}

void ucpd_keepalive(void) {
    if (!s_ka_active) return;
    uint32_t now = xTaskGetTickCount();
    if ((now - s_ka_timer) >= pdMS_TO_TICKS(500)) {
        s_ka_timer = now;
        _send_rdo(s_ka_pdo, s_ka_isel, s_ka_vsел);
    }
}

const char *ucpd_state_name(ucpd_state_t state) {
    switch (state) {
    case UCPD_STATE_IDLE:     return "IDLE";
    case UCPD_STATE_STARTUP:  return "STARTUP";
    case UCPD_STATE_SCAN:     return "SCAN";
    case UCPD_STATE_RUNNING:  return "RUNNING";
    case UCPD_STATE_FAULT:    return "FAULT";
    case UCPD_STATE_RECOVERY: return "RECOVERY";
    default:                  return "UNKNOWN";
    }
}

uint8_t ucpd_read_all_pdos(void) {
    uint8_t raw[26] = {0};
    s_pdo_count = 0;
    if (_read_burst(REG_SRCPDO, raw, 26) != ESP_OK) return 0;

    for (uint8_t i = 0; i < UCPD_MAX_PDOS; i++) {
        uint16_t r = (uint16_t)(raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8));
        ucpd_pdo_t *p = &s_pdos[i];
        p->index = i + 1;
        p->valid = (r >> 15) & 1;
        if (!p->valid) continue;

        p->is_epr = (i >= 7);
        uint8_t type_bit = (r >> 14) & 1;
        p->type = type_bit ? (p->is_epr ? 2 : 1) : 0;  /* 0=Fixed,1=PPS,2=AVS */
        uint8_t curr_code = (r >> 10) & 0x0F;
        p->max_ma = s_current_map[curr_code];
        uint8_t vmax_raw = r & 0xFF;

        if (p->type == 0) {          /* Fixed */
            p->min_mv = 0;
            p->max_mv = (uint16_t)(vmax_raw * (p->is_epr ? 200u : 100u));
        } else if (p->type == 1) {   /* PPS */
            uint8_t vmin_code = (r >> 8) & 0x03;
            p->min_mv = (uint16_t)(vmin_code * 100u + 3300u);
            p->max_mv = (uint16_t)(vmax_raw * 100u);
        } else {                     /* AVS */
            uint8_t vmin_code = (r >> 8) & 0x03;
            p->min_mv = (uint16_t)(vmin_code * 200u + 15000u);
            p->max_mv = (uint16_t)(vmax_raw * 200u);
        }
        s_pdo_count++;
        ESP_LOGI(TAG, "PDO[%u] %s  min=%umV max=%umV max=%umA%s",
                 p->index,
                 p->type == 0 ? "Fixed" : (p->type == 1 ? "PPS" : "AVS"),
                 p->min_mv, p->max_mv, p->max_ma,
                 p->is_epr ? " [EPR]" : "");
    }
    return s_pdo_count;
}

const ucpd_pdo_t *ucpd_get_pdo(uint8_t index) {
    if (index < 1 || index > UCPD_MAX_PDOS) return NULL;
    return s_pdos[index - 1].valid ? &s_pdos[index - 1] : NULL;
}

uint8_t ucpd_get_pdo_count(void) { return s_pdo_count; }

uint8_t ucpd_set_voltage(uint16_t voltage_mv, uint16_t min_current_ma) {
    /* Try PPS first for precise control */
    uint8_t idx = ucpd_set_pps(voltage_mv, min_current_ma);
    if (idx) return idx;

    /* Fall back to Fixed PDO — find nearest that meets current requirement */
    uint8_t best = 0;
    uint16_t best_diff = 0xFFFF;
    for (uint8_t i = 0; i < UCPD_MAX_PDOS; i++) {
        ucpd_pdo_t *p = &s_pdos[i];
        if (!p->valid || p->type != 0) continue;
        if (p->max_ma < min_current_ma) continue;
        uint16_t diff = (p->max_mv >= voltage_mv)
                        ? p->max_mv - voltage_mv
                        : voltage_mv - p->max_mv;
        if (diff < best_diff) { best_diff = diff; best = p->index; }
    }
    if (best) {
        uint8_t isel = _current_encode(s_pdos[best-1].max_ma);
        _send_rdo(best, isel, 0);
        s_ka_is_avs = false;
        ESP_LOGI(TAG, "Fixed PDO %u selected (%umV)", best, s_pdos[best-1].max_mv);
    }
    return best;
}

uint8_t ucpd_set_pps(uint16_t voltage_mv, uint16_t max_current_ma) {
    for (uint8_t i = 0; i < UCPD_MAX_PDOS; i++) {
        ucpd_pdo_t *p = &s_pdos[i];
        if (!p->valid || p->type != 1) continue;
        if (voltage_mv < p->min_mv || voltage_mv > p->max_mv) continue;
        if (p->max_ma < max_current_ma) max_current_ma = p->max_ma;
        uint8_t vsел = (uint8_t)(voltage_mv / UCPD_PPS_VSTEP_MV);
        uint8_t isel = _current_encode(max_current_ma);
        _send_rdo(p->index, isel, vsел);
        s_ka_is_avs = false;
        ESP_LOGI(TAG, "PPS PDO %u selected (%umV / %umA)", p->index, voltage_mv, max_current_ma);
        return p->index;
    }
    return 0;
}

uint8_t ucpd_set_avs(uint16_t voltage_mv, uint16_t max_current_ma) {
    for (uint8_t i = 0; i < UCPD_MAX_PDOS; i++) {
        ucpd_pdo_t *p = &s_pdos[i];
        if (!p->valid || p->type != 2) continue;
        if (voltage_mv < p->min_mv || voltage_mv > p->max_mv) continue;
        if (p->max_ma < max_current_ma) max_current_ma = p->max_ma;
        uint8_t vsел = (uint8_t)(voltage_mv / UCPD_AVS_VSTEP_MV);
        uint8_t isel = _current_encode(max_current_ma);
        _send_rdo(p->index, isel, vsел);
        s_ka_is_avs = true;
        ESP_LOGI(TAG, "AVS PDO %u selected (%umV / %umA)", p->index, voltage_mv, max_current_ma);
        return p->index;
    }
    return 0;
}

uint8_t ucpd_set_5v (uint16_t min_ma) { return ucpd_set_voltage(5000,  min_ma); }
uint8_t ucpd_set_9v (uint16_t min_ma) { return ucpd_set_voltage(9000,  min_ma); }
uint8_t ucpd_set_12v(uint16_t min_ma) { return ucpd_set_voltage(12000, min_ma); }
uint8_t ucpd_set_15v(uint16_t min_ma) { return ucpd_set_voltage(15000, min_ma); }
uint8_t ucpd_set_20v(uint16_t min_ma) { return ucpd_set_voltage(20000, min_ma); }

esp_err_t ucpd_set_output(bool on) {
    return _write8(REG_SYSTEM, on ? SYSTEM_OUTPUT_ON : SYSTEM_OUTPUT_OFF);
}

esp_err_t ucpd_hard_reset(void) {
    s_ka_active = false;
    return _write8(REG_PD_CMDMSG, 0x01);
}

esp_err_t ucpd_wait_negotiation(uint32_t timeout_ms) {
    uint32_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        int16_t r = _read8(REG_PD_MSGRLT);
        if (r >= 0 && (r & MSGRLT_SUCCESS)) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_ERR_TIMEOUT;
}

uint16_t ucpd_get_voltage_mv(void) {
    int32_t v = _read16(REG_VOLTAGE);
    return (v < 0) ? 0 : (uint16_t)((uint16_t)v * 80u);
}

uint16_t ucpd_get_current_ma(void) {
    int16_t v = _read8(REG_CURRENT);
    return (v < 0) ? 0 : (uint16_t)((uint8_t)v * 24u);
}

uint32_t ucpd_get_power_mw(void) {
    return (uint32_t)ucpd_get_voltage_mv() * ucpd_get_current_ma() / 1000;
}

int8_t ucpd_get_temperature_c(void) {
    int16_t v = _read8(REG_TEMP);
    return (v < 0) ? 0 : (int8_t)v;
}

uint16_t ucpd_get_requested_voltage_mv(void) {
    int32_t v = _read16(REG_VREQ);
    return (v < 0) ? 0 : (uint16_t)((uint16_t)v * 50u);
}

uint16_t ucpd_get_requested_current_ma(void) {
    int32_t v = _read16(REG_IREQ);
    return (v < 0) ? 0 : (uint16_t)((uint16_t)v * 10u);
}

bool ucpd_is_pd_connected(void) {
    int16_t v = _read8(REG_OPMODE);
    return (v >= 0) && ((uint8_t)v & OPMODE_PDMOD);
}

bool ucpd_is_legacy_connected(void) {
    int16_t v = _read8(REG_OPMODE);
    return (v >= 0) && ((uint8_t)v & OPMODE_LGCYMOD);
}

bool ucpd_is_cable_flipped(void) {
    int16_t v = _read8(REG_OPMODE);
    return (v >= 0) && ((uint8_t)v & OPMODE_CCFLIP);
}

bool ucpd_is_derating(void) {
    int16_t v = _read8(REG_OPMODE);
    return (v >= 0) && ((uint8_t)v & OPMODE_DR);
}

bool ucpd_is_fault(void) {
    return (ucpd_get_fault_flags() & STATUS_FAULTS) != 0;
}

uint8_t ucpd_get_fault_flags(void) {
    int16_t v = _read8(REG_STATUS);
    return (v < 0) ? 0 : ((uint8_t)v & STATUS_FAULTS);
}

esp_err_t ucpd_set_ovp_offset_mv(uint16_t offset_mv) {
    return _write8(REG_OVPTHR, (uint8_t)(offset_mv / 80));
}

esp_err_t ucpd_set_uvp_threshold(uint8_t mode) {
    return _write8(REG_UVPTHR, mode);
}

esp_err_t ucpd_set_ocp_threshold_ma(uint16_t ma) {
    return _write8(REG_OCPTHR, (uint8_t)(ma / 50));
}

esp_err_t ucpd_set_otp_threshold_c(uint8_t deg_c) {
    return _write8(REG_OTPTHR, deg_c);
}
