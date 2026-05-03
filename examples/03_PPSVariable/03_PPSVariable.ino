/**
 * 03_PPSVariable.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Sweep output voltage up and down using PPS (SPR) or AVS (EPR) if available.
 * Demonstrates the keep-alive requirement: pd.task() must be called in loop().
 *
 * Connect a multimeter or oscilloscope to VOUT to observe the sweep.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include "AP33772S.h"

#define PIN_SDA  20
#define PIN_SCL  21
#define PIN_INT   6

AP33772S pd(Wire, PIN_INT);

// Sweep parameters
static const uint16_t SWEEP_CURRENT_MA = 2000;   // Request up to 2 A
static const uint32_t STEP_DELAY_MS    = 300;     // ms between voltage steps
static const uint16_t STEP_SIZE_MV     = 500;     // 500 mV per step

// Current sweep state
static bool     sweepActive  = false;
static bool     sweepUp      = true;
static uint16_t sweepVoltage = 0;
static uint16_t sweepMin     = 0;
static uint16_t sweepMax     = 0;
static uint16_t sweepStep    = PPS_VSTEP_MV;
static int8_t   sweepPDOIdx  = -1;
static uint8_t  sweepType    = PDO_TYPE_FIXED;
static uint32_t lastStepTime = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println(F("\n=== AP33772S PPS/AVS Voltage Sweep ===\n"));

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    if (pd.begin() != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found."));
        while (true);
    }

    Serial.println(F("Waiting for cable..."));
    pd.waitForStartup(10000);
    pd.waitForPDOs(10000);
    pd.readAllPDOs();
    pd.printPDOs();

    // ── Prefer AVS > PPS > Fixed for the sweep ────────────────────────────
    sweepPDOIdx = pd.getAVSIndex();
    if (sweepPDOIdx > 0) {
        const AP33772S_PDO &p = pd.getPDO(sweepPDOIdx);
        sweepMin  = p.minVoltage_mV;
        sweepMax  = p.maxVoltage_mV;
        sweepStep = AVS_VSTEP_MV;
        sweepType = PDO_TYPE_AVS;
        Serial.printf("Using AVS PDO %u: %u–%u mV (%u mV steps)\n",
                      sweepPDOIdx, sweepMin, sweepMax, sweepStep);
    } else {
        sweepPDOIdx = pd.getPPSIndex();
        if (sweepPDOIdx > 0) {
            const AP33772S_PDO &p = pd.getPDO(sweepPDOIdx);
            sweepMin  = p.minVoltage_mV;
            sweepMax  = p.maxVoltage_mV;
            sweepStep = PPS_VSTEP_MV;
            sweepType = PDO_TYPE_PPS;
            Serial.printf("Using PPS PDO %u: %u–%u mV (%u mV steps)\n",
                          sweepPDOIdx, sweepMin, sweepMax, sweepStep);
        } else {
            Serial.println(F("[WARN] No PPS/AVS available, using Fixed."));
            sweepPDOIdx = 1;
            sweepType   = PDO_TYPE_FIXED;
        }
    }

    // Start sweep from minimum voltage
    sweepVoltage = sweepMin;
    sweepUp      = true;
    sweepActive  = true;

    Serial.printf("\nStarting sweep: %u → %u mV (step %u mV each %u ms)\n",
                  sweepMin, sweepMax, STEP_SIZE_MV, STEP_DELAY_MS);
    Serial.println(F("Note: sweep step may be rounded by hardware to 100 mV (PPS) or 200 mV (AVS).\n"));
}

void loop() {
    pd.task();   // ← REQUIRED for PPS/AVS keep-alive

    // ── Perform sweep step ──────────────────────────────────────────────────
    if (sweepActive && millis() - lastStepTime >= STEP_DELAY_MS) {
        lastStepTime = millis();

        // Move to next voltage
        if (sweepUp) {
            sweepVoltage += STEP_SIZE_MV;
            if (sweepVoltage >= sweepMax) {
                sweepVoltage = sweepMax;
                sweepUp = false;  // Reverse direction
            }
        } else {
            sweepVoltage -= STEP_SIZE_MV;
            if (sweepVoltage <= sweepMin) {
                sweepVoltage = sweepMin;
                sweepUp = true;  // Reverse direction
            }
        }

        // Issue RDO
        if (sweepType == PDO_TYPE_AVS) {
            pd.setAVSPDO(sweepPDOIdx, sweepVoltage, SWEEP_CURRENT_MA);
        } else if (sweepType == PDO_TYPE_PPS) {
            pd.setPPSPDO(sweepPDOIdx, sweepVoltage, SWEEP_CURRENT_MA);
        } else {
            pd.setFixPDO(sweepPDOIdx, SWEEP_CURRENT_MA);
        }

        Serial.printf("Step: target %u mV  →  actual V=%u mV, I=%u mA, P=%u mW, T=%d°C\n",
                      sweepVoltage,
                      pd.getVoltage_mV(), pd.getCurrent_mA(),
                      pd.getPower_mW(),   pd.getTemperature_C());
    }

    delay(100);
}
