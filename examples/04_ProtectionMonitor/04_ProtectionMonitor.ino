/**
 * 04_ProtectionMonitor.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Interrupt-driven fault detection with automatic recovery.
 *
 * Configures protection thresholds and monitors faults via INT pin.
 * Demonstrates setOVPOffset_mV(), setUVPThreshold(), setOCPThreshold_mA(), etc.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include "AP33772S.h"

#define PIN_SDA  20
#define PIN_SCL  21
#define PIN_INT   6

AP33772S pd(Wire, PIN_INT);

volatile bool intFired = false;

void IRAM_ATTR onInterrupt() {
    intFired = true;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println(F("\n=== AP33772S Protection Monitor ===\n"));

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    if (pd.begin() != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found."));
        while (true);
    }

    // ── Configure protection thresholds ─────────────────────────────────────
    // OVP: 2000 mV above negotiated voltage (80 mV LSB)
    pd.setOVPOffset_mV(2000);
    Serial.println(F("OVP: +2000 mV offset from VREQ"));

    // UVP: 80% threshold
    pd.setUVPThreshold(UVP_80PCT);
    Serial.println(F("UVP: 80% of VREQ"));

    // OCP: auto-set to 110% of negotiated PDO max
    pd.setOCPThreshold_mA(0);
    Serial.println(F("OCP: auto (110% PDO max)"));

    // OTP: 100°C limit
    pd.setOTPThreshold_C(100);
    Serial.println(F("OTP: 100°C"));

    // Thermal derating kicks in at 80°C
    pd.setDeratingThreshold_C(80);
    Serial.println(F("Derating: 80°C"));

    // Enable all protections and interrupts
    pd.setProtectionConfig(true, true, true, true, true);
    pd.setInterruptMask(MASK_ALL);
    pd.attachInterruptCallback(onInterrupt);

    Serial.println(F("\n=== Initialization Complete ===\n"));
    Serial.println(F("Waiting for cable..."));

    pd.waitForStartup(10000);
    pd.waitForPDOs(10000);
    pd.readAllPDOs();

    // Request 12V
    uint8_t idx = pd.setVoltage(12000, 1000);
    if (idx) {
        pd.waitForNegotiation(3000);
        Serial.printf("Negotiated PDO %u @ %u mV / %u mA\n",
                      idx, pd.getRequestedVoltage_mV(), pd.getRequestedCurrent_mA());
    }
}

void loop() {
    pd.task();

    // ── Interrupt-driven fault handling ─────────────────────────────────────
    if (intFired) {
        intFired = false;

        uint8_t status = pd.getInterruptCause();  // Clears STATUS

        if (status) {
            Serial.printf("Interrupt: 0x%02X  ", status);

            if (status & STATUS_STARTED) Serial.print(F("[STARTED] "));
            if (status & STATUS_READY)   Serial.print(F("[READY] "));
            if (status & STATUS_NEWPDO)  Serial.print(F("[NEWPDO] "));
            if (status & STATUS_UVP)     Serial.print(F("[UVP-FAULT] "));
            if (status & STATUS_OVP)     Serial.print(F("[OVP-FAULT] "));
            if (status & STATUS_OCP)     Serial.print(F("[OCP-FAULT] "));
            if (status & STATUS_OTP)     Serial.print(F("[OTP-FAULT] "));

            Serial.println();

            // Handle fault recovery
            if (status & STATUS_FAULTS) {
                Serial.printf("FAULT LATCHED: %s\n", pd.getFaultString().c_str());
                Serial.printf("  V=%u mV  I=%u mA  T=%d°C\n",
                              pd.getVoltage_mV(), pd.getCurrent_mA(),
                              pd.getTemperature_C());
                Serial.println(F("Attempting recovery...");

                delay(2000);
                pd.issueHardReset();
                Serial.println(F("Hard reset issued."));
            } else if (status & STATUS_NEWPDO) {
                Serial.println(F("Re-scanning PDOs..."));
                pd.readAllPDOs();
                pd.printPDOs();
            }
        }
    }

    // ── Periodic status ────────────────────────────────────────────────────
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 2000) {
        lastPrint = millis();

        uint16_t v = pd.getVoltage_mV();
        uint16_t i = pd.getCurrent_mA();
        uint32_t p = pd.getPower_mW();
        int8_t   t = pd.getTemperature_C();
        bool     dr = pd.isDerating();

        Serial.printf("V=%5u mV  I=%4u mA  P=%6u mW  T=%3d°C%s\n",
                      v, i, p, t, dr ? "  [DERATE]" : "");
    }

    delay(50);
}
