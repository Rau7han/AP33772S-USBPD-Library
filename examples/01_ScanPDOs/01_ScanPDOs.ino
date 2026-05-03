/**
 * 01_ScanPDOs.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Connect to any USB-C charger and print every advertised Power Data Object.
 *
 * Hardware:
 *   PicoPD Pro (RP2040) : SDA=GP20, SCL=GP21, INT=GP6
 *   Any RP2040 board    : adjust Wire.begin(SDA, SCL) accordingly
 *   ESP32               : SDA=GPIO21, SCL=GPIO22
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include "AP33772S.h"

// ── Pin definitions (PicoPD Pro defaults) ─────────────────────────────────────
#define PIN_SDA   20
#define PIN_SCL   21
#define PIN_INT    6

AP33772S pd(Wire, PIN_INT);

void setup() {
    Serial.begin(115200);
    delay(1500);                    // Wait for Serial on RP2040 USB CDC
    Serial.println(F("\n=== AP33772S PDO Scanner ===\n"));

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    // Initialise: enable EPR and PPS/AVS
    if (pd.begin(true, true) != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found on I2C bus. Check wiring."));
        while (true) delay(1000);
    }

    Serial.println(F("Waiting for cable attachment..."));
    if (pd.waitForStartup(10000) != AP33772S_OK) {
        Serial.println(F("[ERR] Timeout — no USB-C cable detected."));
        while (true) delay(1000);
    }
    Serial.println(F("Cable attached! Waiting for PD source capabilities..."));

    if (pd.waitForPDOs(10000) != AP33772S_OK) {
        Serial.println(F("[WARN] Timeout waiting for PDOs — may be legacy charger."));
    }

    // ── Read and print all PDOs ────────────────────────────────────────────
    uint8_t count = pd.readAllPDOs();
    Serial.printf("\n%u valid PDO(s) found:\n\n", count);
    pd.printPDOs();

    // ── Print connection details ──────────────────────────────────────────
    Serial.println();
    if (pd.isPDConnected())     Serial.println(F("Source: USB PD"));
    if (pd.isLegacyConnected()) Serial.println(F("Source: Legacy (non-PD) charger"));
    Serial.printf("Cable orientation: CC%s active\n", pd.isCableFlipped() ? "2" : "1");
    Serial.printf("PPS available: %s\n", pd.hasPPS() ? "YES" : "no");
    Serial.printf("AVS available: %s\n", pd.hasAVS() ? "YES" : "no");

    // ── Dump raw PDO values ────────────────────────────────────────────────
    Serial.println(F("\nRaw PDO register dump:"));
    for (uint8_t i = 1; i <= 13; i++) {
        const AP33772S_PDO &p = pd.getPDO(i);
        if (!p.valid) continue;
        Serial.printf("  PDO%2u [0x%04X] %s minV=%5umV maxV=%5umV maxI=%4umA\n",
                      i, p.raw,
                      p.type == PDO_TYPE_PPS ? "PPS" :
                      p.type == PDO_TYPE_AVS ? "AVS" : "FIX",
                      p.minVoltage_mV, p.maxVoltage_mV, p.maxCurrent_mA);
    }
}

void loop() {
    // Nothing to do — all work done in setup()
    delay(5000);
}
