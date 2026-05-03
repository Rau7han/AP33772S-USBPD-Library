/**
 * 02_RequestVoltage.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Auto-negotiate a target voltage using setVoltage().
 * The library picks the best available PDO (prefers PPS/AVS for exact voltage,
 * falls back to the nearest Fixed PDO).
 *
 * Prints live VOUT / IOUT / power / temperature every second.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include "AP33772S.h"

#define PIN_SDA   20
#define PIN_SCL   21
#define PIN_INT    6

// ── Target voltage and minimum acceptable current ─────────────────────────────
static const uint16_t TARGET_MV  = 12000;  // 12 V
static const uint16_t MIN_MA     =  2000;  // 2 A minimum

AP33772S pd(Wire, PIN_INT);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println(F("\n=== AP33772S Voltage Request Demo ===\n"));

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    if (pd.begin() != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found. Halting."));
        while (true);
    }

    Serial.println(F("Waiting for cable..."));
    pd.waitForStartup(10000);
    pd.waitForPDOs(10000);
    pd.readAllPDOs();

    Serial.println(F("\nAvailable PDOs:"));
    pd.printPDOs();

    // ── Request the target voltage ────────────────────────────────────────
    Serial.printf("\nRequesting %u mV (min %u mA)...\n", TARGET_MV, MIN_MA);
    uint8_t selectedIdx = pd.setVoltage(TARGET_MV, MIN_MA);

    if (selectedIdx == 0) {
        Serial.println(F("[ERR] No suitable PDO found for this voltage/current."));
        while (true);
    }

    const AP33772S_PDO &chosen = pd.getPDO(selectedIdx);
    Serial.printf("Selected PDO %u: %s  maxV=%u mV  maxI=%u mA\n",
                  selectedIdx,
                  chosen.type == PDO_TYPE_PPS ? "PPS" :
                  chosen.type == PDO_TYPE_AVS ? "AVS" : "Fixed",
                  chosen.maxVoltage_mV, chosen.maxCurrent_mA);

    // Wait for PD handshake to complete
    Serial.print(F("Negotiating..."));
    if (pd.waitForNegotiation(3000) == AP33772S_OK) {
        Serial.println(F(" SUCCESS"));
    } else {
        Serial.println(F(" FAILED (check charger compatibility)"));
    }

    Serial.printf("VREQ = %u mV  |  IREQ = %u mA\n",
                  pd.getRequestedVoltage_mV(), pd.getRequestedCurrent_mA());
}

void loop() {
    pd.task();   // ← REQUIRED for PPS/AVS keep-alive

    Serial.printf("V=%5u mV  I=%4u mA  P=%6u mW  T=%d°C",
                  pd.getVoltage_mV(), pd.getCurrent_mA(),
                  pd.getPower_mW(),   pd.getTemperature_C());

    if (pd.isFault())    Serial.printf("  ⚠ %s", pd.getFaultString().c_str());
    if (pd.isDerating()) Serial.print(F("  [DERATE]"));
    Serial.println();

    delay(1000);
}
