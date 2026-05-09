# Spark Analyzer Developer Guide

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                    ESP32 (FreeRTOS)                  │
│                                                     │
│  Core 1 (real-time)      Core 0 (networking)        │
│  ┌─────────────────┐     ┌───────────────────────┐  │
│  │  ucpd_task      │     │  wifi_manager_task    │  │
│  │  (PD state      │     │  (STA/AP, mDNS)       │  │
│  │   machine +     │     ├───────────────────────┤  │
│  │   keep-alive)   │     │  api_task             │  │
│  ├─────────────────┤     │  (HTTP REST server)   │  │
│  │  sensor_task    │     ├───────────────────────┤  │
│  │  (ADC sampling  │     │  ble_task             │  │
│  │   20 Hz)        │     │  (NimBLE GATT)        │  │
│  ├─────────────────┤     ├───────────────────────┤  │
│  │  monitor_task   │     │  (TCP/IP stack)       │  │
│  │  (status log)   │     └───────────────────────┘  │
│  └─────────────────┘                                 │
│                                                     │
│         Shared: spark_measurements_t + mutex         │
└─────────────────────────────────────────────────────┘
         │                          │
         ▼                          ▼
   AP33772S (I2C)          Wi-Fi / BLE Radio
   INA219 (I2C, optional)
```

---

## Module Descriptions

### `ucpd_controller.c/h`
USB-C Power Delivery controller. Drives the AP33772S over I2C.

**Key responsibilities:**
- I2C initialisation and register access
- PD state machine: IDLE → STARTUP → SCAN → RUNNING → FAULT → RECOVERY
- PDO discovery and decoding (Fixed, PPS, AVS)
- RDO (Request Data Object) construction and transmission
- PPS/AVS 500ms keep-alive
- Protection threshold configuration

**State machine:**
```
IDLE ──▶ STARTUP ──▶ SCAN ──▶ RUNNING ◀──┐
                               │  ▲       │
                               ▼  │       │
                             FAULT       NEW_PDO
                               │
                               ▼
                            RECOVERY ──▶ STARTUP
```

### `wifi_manager.c/h`
Wi-Fi connectivity management.

**Features:**
- Station (STA) mode with automatic reconnection (exponential back-off)
- Access Point (AP) fallback for first-run provisioning
- Credentials persistence in NVS (Non-Volatile Storage)
- mDNS registration at `http://spark.local/`
- Thread-safe state query API

### `ble_manager.c/h`
BLE GATT server using ESP-IDF NimBLE stack.

**GATT structure:**
- Environmental Sensing Service (`0x181A`) with notify characteristics
- Custom Power Control Service with writable characteristics
- Thread-safe measurement updates and command queue

### `api_server.c/h`
HTTP REST API using `esp_http_server`.

**Handler pattern:**
```c
static esp_err_t _handle_xxx(httpd_req_t *req) {
    // 1. Set CORS headers
    // 2. Read request body (for POST)
    // 3. Parse JSON with cJSON
    // 4. Call ucpd_*/power_supply_* functions
    // 5. Build JSON response with cJSON
    // 6. Send response
    return ESP_OK;
}
```

### `power_supply.c/h`
Higher-level programmable power supply abstraction.

**Provides:**
- Voltage clamping and hardware step rounding
- Mode tracking (Fixed/PPS/AVS)
- Step-up / step-down voltage control
- Output enable/disable

### `current_sensor.c/h`
Current and voltage measurement.

**Priority order:**
1. External INA219 over I2C (higher accuracy, ±0.5% typical)
2. AP33772S internal ADC (24 mA LSB, sufficient for most uses)

**Features:**
- Moving average (8 samples) for stable readings
- Zero-offset calibration
- Shared I2C bus with AP33772S

### `config_manager.c/h`
NVS-based persistent configuration.

---

## Build System

### Arduino IDE (Example Sketch)

The `examples/06_SparkAnalyzer/` directory contains a self-contained Arduino sketch:

```
06_SparkAnalyzer/
├── 06_SparkAnalyzer.ino   ← Main sketch (setup/loop)
├── wifi_manager.h         ← WiFi manager (header-only)
├── ble_manager.h          ← BLE manager (header-only)
├── api_server.h           ← REST API server (header-only)
└── web_dashboard.h        ← Embedded HTML dashboard
```

Required board: **esp32** by Espressif Systems (install via Board Manager)

### ESP-IDF

```bash
# Prerequisites: ESP-IDF 5.x installed and activated
cd SparkAnalyzer/firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**Component dependencies** (managed automatically by CMakeLists.txt):
- `driver` — I2C, GPIO
- `esp_wifi`, `esp_event`, `esp_netif` — Wi-Fi
- `mdns` — mDNS (spark.local)
- `bt`, `nimble` — BLE
- `esp_http_server` — HTTP REST API
- `nvs_flash` — Configuration persistence
- `json` — cJSON for API responses

---

## Adding New API Endpoints

1. Define a handler function in `api_server.c`:
   ```c
   static esp_err_t _handle_my_endpoint(httpd_req_t *req) {
       _set_cors(req);
       // ... your logic
       httpd_resp_send(req, "{\"ok\":true}", -1);
       return ESP_OK;
   }
   ```

2. Register it in `api_server_start()`:
   ```c
   httpd_uri_t my_route = {
       "/api/my_endpoint", HTTP_GET, _handle_my_endpoint, NULL
   };
   httpd_register_uri_handler(s_server, &my_route);
   ```

---

## Modifying PD Behavior

Override the auto-negotiation logic in `ucpd_run()` (`UCPD_STATE_SCAN` case):
```c
case UCPD_STATE_SCAN: {
    uint8_t n = ucpd_read_all_pdos();
    // Custom priority — e.g., always prefer PPS
    uint8_t idx = ucpd_set_pps(9000, 3000);
    if (!idx) idx = ucpd_set_voltage(9000, 1000);
    if (!idx) idx = ucpd_set_5v(500);
    // ...
}
```

---

## Thread Safety Notes

- `spark_measurements_t` is protected by `s_meas_mutex` (FreeRTOS mutex)
- All BLE characteristic callbacks acquire `s_mutex` before writing commands
- `ucpd_*` functions are called only from `ucpd_task` (Core 1) — no additional locking needed for I2C
- `wifi_manager_set_credentials()` is safe to call from any task (uses event groups)

---

## Adding OTA (Over-The-Air Updates)

ESP-IDF includes the `esp_https_ota` component. Add to your CMakeLists.txt:
```cmake
REQUIRES esp_https_ota
```

Example OTA handler:
```c
#include "esp_https_ota.h"

static esp_err_t _handle_ota(httpd_req_t *req) {
    esp_http_client_config_t cfg = {
        .url = "http://your-server/firmware.bin",
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &cfg };
    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        httpd_resp_send(req, "{\"ok\":true,\"message\":\"OTA success\"}", -1);
        esp_restart();
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed");
    }
    return ESP_OK;
}
```

---

## Logging

All modules use ESP-IDF's `esp_log` with module-specific tags:

| Tag | Module |
|-----|--------|
| `SparkAnalyzer` | main.c |
| `UCPD` | ucpd_controller.c |
| `WiFiMgr` | wifi_manager.c |
| `BLEMgr` | ble_manager.c |
| `APIServer` | api_server.c |
| `PowerSupply` | power_supply.c |
| `Sensor` | current_sensor.c |
| `Config` | config_manager.c |

Change log level in `menuconfig` → Component config → Log output, or at runtime:
```c
esp_log_level_set("UCPD", ESP_LOG_VERBOSE);
```

---

## Performance Characteristics

| Metric | Typical Value |
|--------|--------------|
| API response latency | < 20 ms (LAN) |
| Measurement update rate | 20 Hz (50 ms) |
| BLE notification interval | 500 ms |
| PPS keep-alive interval | 500 ms |
| Wi-Fi reconnect time | 2–5 s |
| Boot to PD ready | ~3 s |

---

*For API endpoint documentation see [API_REFERENCE.md](API_REFERENCE.md)*  
*For hardware and first-time setup see [USER_GUIDE.md](USER_GUIDE.md)*
