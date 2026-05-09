/**
 * 06_SparkAnalyzer.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Spark Analyzer — Wi-Fi/BLE enabled USB-C PD Analyzer & Programmable Power
 * Supply firmware for ESP32 + AP33772S.
 *
 * Features:
 *  • USB-C PD negotiation: Fixed (5V/9V/12V/15V/20V) and PPS/AVS modes
 *  • PPS output: 3.3 V – 21 V in 100 mV hardware steps
 *  • Wi-Fi (STA with AP-mode provisioning fallback) + mDNS (spark.local)
 *  • BLE GATT server for mobile app connectivity
 *  • REST API + embedded live web dashboard on port 80
 *  • Interrupt-driven fault detection with automatic recovery
 *  • State machine: IDLE → STARTUP → SCAN → RUNNING → FAULT → RECOVERY
 *
 * Hardware (Spark Analyzer / ESP32 devkit):
 *   AP33772S   SDA → GPIO 21
 *   AP33772S   SCL → GPIO 22
 *   AP33772S   INT → GPIO 19   (optional — set PIN_INT to -1 to disable)
 *
 * Wi-Fi provisioning:
 *   On first boot (no saved credentials) the device starts a Wi-Fi access
 *   point named "SparkAnalyzer" (password: spark1234).
 *   Connect to it and visit http://192.168.4.1/ to configure your network.
 *
 * REST API base URL (after Wi-Fi connection): http://spark.local/api/...
 *   GET  /api/status  — measurements + connection info
 *   GET  /api/pdos    — available PDOs
 *   POST /api/voltage — {"voltage_mv":12000,"current_ma":2000}
 *   POST /api/pps     — {"voltage_mv":9500,"current_ma":3000}
 *   POST /api/avs     — {"voltage_mv":20000,"current_ma":3000}
 *   POST /api/output  — {"on":true}
 *   POST /api/reset   — hard reset
 *   POST /api/wifi    — {"ssid":"MyNet","pass":"secret"}
 *
 * Required boards: "esp32" by Espressif (Board Manager)
 * Required libraries: This AP33772S library (already included)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "AP33772S.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "api_server.h"

// ── Pin definitions (adjust to match your hardware) ───────────────────────────
#define PIN_SDA   21    // I2C SDA
#define PIN_SCL   22    // I2C SCL
#define PIN_INT   19    // AP33772S INT (active-low); set -1 to poll instead

// ── Wi-Fi AP fallback credentials (for provisioning mode) ────────────────────
#define SPARK_AP_SSID     "SparkAnalyzer"
#define SPARK_AP_PASSWORD "spark1234"

// ── mDNS hostname — device reachable at http://spark.local/ ──────────────────
#define MDNS_HOSTNAME     "spark"

// ─────────────────────────────────────────────────────────────────────────────
//  Global objects
// ─────────────────────────────────────────────────────────────────────────────
AP33772S    pd(Wire, PIN_INT);
WifiManager wifiMgr(SPARK_AP_SSID, SPARK_AP_PASSWORD);
BLEManager  bleMgr("SparkAnalyzer");
APIServer   apiSrv(pd, &wifiMgr);

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, STARTUP, SCAN, RUNNING, FAULT, RECOVERY };
static State state = IDLE;

// ── Interrupt flag ────────────────────────────────────────────────────────────
volatile bool intFired = false;
void AP33772S_ISR_ATTR onInt() { intFired = true; }

// ─────────────────────────────────────────────────────────────────────────────
//  Helper — print compact status line
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lastPrintMs = 0;
void printStatus() {
    if (millis() - lastPrintMs < 2000) return;
    lastPrintMs = millis();
    Serial.printf("[Spark] V=%5u mV  I=%4u mA  P=%6lu mW  T=%3d°C%s\n",
        pd.getVoltage_mV(), pd.getCurrent_mA(),
        (unsigned long)pd.getPower_mW(), pd.getTemperature_C(),
        pd.isDerating() ? "  [DERATE]" : "");
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println(F("\n╔══════════════════════════════╗"));
    Serial.println(F("║    Spark Analyzer v1.0       ║"));
    Serial.println(F("║  ESP32 + AP33772S USB-C PD   ║"));
    Serial.println(F("╚══════════════════════════════╝\n"));

    // ── I2C + AP33772S ─────────────────────────────────────────────────────
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    if (pd.begin(/*enableEPR=*/true, /*enablePPS=*/true) != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found on I2C. Check SDA/SCL wiring."));
        // Continue — device may not have PD source connected yet
    }

    // Protection defaults
    pd.setOVPOffset_mV(2000);           // OVP = VREQ + 2 V
    pd.setUVPThreshold(UVP_80PCT);
    pd.setOCPThreshold_mA(0);           // Auto: 110% of PDO max
    pd.setOTPThreshold_C(100);
    pd.setDeratingThreshold_C(80);
    pd.setInterruptMask(MASK_ALL);
    if (PIN_INT >= 0) {
        pd.attachInterruptCallback(onInt);
    }

    // ── Wi-Fi ──────────────────────────────────────────────────────────────
    wifiMgr.begin();

    // ── BLE ────────────────────────────────────────────────────────────────
    bleMgr.begin();

    // ── REST API server ────────────────────────────────────────────────────
    // Note: API server is always started. It works in both STA and AP modes.
    apiSrv.begin();

    // ── mDNS (spark.local) ─────────────────────────────────────────────────
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Device reachable at http://%s.local/\n", MDNS_HOSTNAME);
    }

    // ── Start PD state machine ─────────────────────────────────────────────
    state = STARTUP;
    Serial.println(F("[PD] Waiting for USB-C cable..."));
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    // ── Mandatory PPS/AVS keep-alive ───────────────────────────────────────
    pd.task();

    // ── Wi-Fi manager (reconnection / AP fallback) ─────────────────────────
    wifiMgr.loop();

    // ── HTTP REST API ──────────────────────────────────────────────────────
    apiSrv.loop();

    // ── BLE measurements push ──────────────────────────────────────────────
    bleMgr.updateMeasurements(
        pd.getVoltage_mV(), pd.getCurrent_mA(),
        pd.getPower_mW(),   pd.getTemperature_C());

    // ── Handle pending BLE commands ────────────────────────────────────────
    if (bleMgr.hasVoltageRequest()) {
        uint16_t mv = bleMgr.getPendingVoltage_mV();
        uint16_t ma = bleMgr.hasCurrentRequest()
                      ? bleMgr.getPendingCurrent_mA() : 1000;
        uint8_t idx = pd.setVoltage(mv, ma);
        Serial.printf("[BLE→PD] Voltage request %u mV → PDO %u\n", mv, idx);
    }
    if (bleMgr.hasOutputRequest()) {
        bool on = bleMgr.getPendingOutputState();
        pd.setOutput(on);
        Serial.printf("[BLE→PD] Output %s\n", on ? "ON" : "OFF");
    }

    // ── PD state machine ───────────────────────────────────────────────────
    switch (state) {

    case STARTUP:
        if (pd.waitForStartup(/*timeoutMs=*/100) == AP33772S_OK) {
            Serial.println(F("[PD] Cable attached — waiting for PDOs..."));
            state = SCAN;
        }
        break;

    case SCAN: {
        int8_t r = pd.waitForPDOs(/*timeoutMs=*/100);
        if (r == AP33772S_OK) {
            uint8_t n = pd.readAllPDOs();
            Serial.printf("[PD] %u PDO(s) discovered:\n", n);
            pd.printPDOs();

            // Auto-negotiate: prefer 20V, fall back through 12V → 5V
            uint8_t idx = pd.setVoltage(20000, 1500);
            if (!idx) idx = pd.setVoltage(12000, 1000);
            if (!idx) idx = pd.set5V(500);

            if (idx && pd.waitForNegotiation(3000) == AP33772S_OK) {
                Serial.printf("[PD] Negotiated %u mV / %u mA (PDO %u)\n",
                              pd.getRequestedVoltage_mV(),
                              pd.getRequestedCurrent_mA(), idx);
                state = RUNNING;
            } else {
                Serial.println(F("[PD] Negotiation failed — retrying..."));
            }
        }
        break;
    }

    case RUNNING:
        // Interrupt-driven fault / new-PDO detection
        if (PIN_INT >= 0 && intFired) {
            intFired = false;
            uint8_t cause = pd.clearInterrupt();
            if (cause & STATUS_FAULTS) {
                Serial.printf("[PD] FAULT: %s  V=%u mA=%u T=%d\n",
                              pd.getFaultString().c_str(),
                              pd.getVoltage_mV(), pd.getCurrent_mA(),
                              pd.getTemperature_C());
                state = FAULT;
            } else if (cause & STATUS_NEWPDO) {
                Serial.println(F("[PD] New PDOs received — re-scanning"));
                state = SCAN;
            }
        } else if (PIN_INT < 0) {
            // Polling fallback (no INT pin)
            if (pd.isFault()) {
                Serial.printf("[PD] FAULT (polled): %s\n",
                              pd.getFaultString().c_str());
                state = FAULT;
            }
        }
        printStatus();
        break;

    case FAULT:
        Serial.println(F("[PD] Attempting recovery in 3 s..."));
        delay(3000);
        state = RECOVERY;
        break;

    case RECOVERY: {
        uint16_t targetV = pd.getRequestedVoltage_mV();
        if (targetV == 0) targetV = 5000;
        uint8_t idx = pd.setVoltage(targetV, 1000);
        if (idx && pd.waitForNegotiation(3000) == AP33772S_OK) {
            Serial.println(F("[PD] Recovery successful"));
            state = RUNNING;
        } else {
            Serial.println(F("[PD] Recovery failed — issuing hard reset"));
            pd.issueHardReset();
            delay(500);
            state = STARTUP;
        }
        break;
    }

    case IDLE:
    default:
        break;
    }
}
