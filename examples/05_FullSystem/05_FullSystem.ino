/**
 * 05_FullSystem.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Full state-machine with Serial command interface.
 *
 * States: IDLE → STARTUP → SCAN → RUNNING → FAULT → RECOVERY
 *
 * Serial commands (newline-terminated):
 *   scan        — re-read and print all PDOs
 *   status      — print current status and measurements
 *   regs        — dump all I2C registers
 *   set <mV>    — request voltage in mV (e.g. "set 12000")
 *   set <mV> <mA> — request with minimum current (e.g. "set 15000 3000")
 *   pps <mV>    — force PPS mode (e.g. "pps 9500")
 *   avs <mV>    — force AVS mode (e.g. "avs 24000")
 *   5v / 9v / 12v / 15v / 20v / 28v — quick voltage shortcuts
 *   out on|off  — control VOUT switch
 *   reset       — issue PD hard reset
 *   ntc         — print NTC temperature
 *   help        — list commands
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include "AP33772S.h"

#define PIN_SDA  20
#define PIN_SCL  21
#define PIN_INT   6

AP33772S pd(Wire, PIN_INT);

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, STARTUP, SCAN, RUNNING, FAULT, RECOVERY };
static State state = IDLE;

volatile bool intFired = false;
void IRAM_ATTR onInt() { intFired = true; }

// ─────────────────────────────────────────────────────────────────────────────
void printHelp() {
    Serial.println(F("\nCommands:"));
    Serial.println(F("  scan           — re-scan PDOs"));
    Serial.println(F("  status         — measurements + status"));
    Serial.println(F("  regs           — dump registers"));
    Serial.println(F("  set <mV> [mA]  — request voltage"));
    Serial.println(F("  pps <mV> [mA]  — force PPS"));
    Serial.println(F("  avs <mV> [mA]  — force AVS"));
    Serial.println(F("  5v 9v 12v 15v 20v 28v"));
    Serial.println(F("  out on|off     — VOUT switch"));
    Serial.println(F("  reset          — PD hard reset"));
    Serial.println(F("  ntc            — print temperature"));
    Serial.println(F("  help\n"));
}

void printStatus() {
    Serial.printf("State  : %s\n",
        state == IDLE     ? "IDLE"     :
        state == STARTUP  ? "STARTUP"  :
        state == SCAN     ? "SCAN"     :
        state == RUNNING  ? "RUNNING"  :
        state == FAULT    ? "FAULT"    : "RECOVERY");
    Serial.printf("Source : %s%s\n",
        pd.isPDConnected()     ? "USB PD " : "",
        pd.isLegacyConnected() ? "Legacy"  : "");
    Serial.printf("CC     : CC%s\n",      pd.isCableFlipped() ? "2" : "1");
    Serial.printf("Derate : %s\n",        pd.isDerating()     ? "YES" : "no");
    Serial.printf("Fault  : %s\n",        pd.getFaultString().c_str());
    Serial.printf("VREQ   : %5u mV\n",   pd.getRequestedVoltage_mV());
    Serial.printf("IREQ   : %5u mA\n",   pd.getRequestedCurrent_mA());
    Serial.printf("VOUT   : %5u mV\n",   pd.getVoltage_mV());
    Serial.printf("IOUT   : %5u mA\n",   pd.getCurrent_mA());
    Serial.printf("POUT   : %5u mW\n",   pd.getPower_mW());
    Serial.printf("TEMP   : %5d °C\n\n", pd.getTemperature_C());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Command processor
// ─────────────────────────────────────────────────────────────────────────────
void processCommand(String cmd) {
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "help")   { printHelp(); return; }
    if (cmd == "status") { printStatus(); return; }
    if (cmd == "regs")   { pd.dumpRegisters(); return; }
    if (cmd == "ntc")    { Serial.printf("Temp: %d°C\n", pd.getTemperature_C()); return; }

    if (cmd == "scan") {
        uint8_t n = pd.readAllPDOs();
        Serial.printf("%u PDO(s):\n", n);
        pd.printPDOs();
        return;
    }

    if (cmd == "reset") {
        pd.issueHardReset();
        Serial.println(F("Hard reset issued."));
        state = STARTUP;
        return;
    }

    if (cmd == "out on")  { pd.setOutput(true);  Serial.println(F("VOUT ON"));  return; }
    if (cmd == "out off") { pd.setOutput(false); Serial.println(F("VOUT OFF")); return; }

    // Quick voltage shortcuts
    struct { const char *cmd; uint16_t mv; } shortcuts[] = {
        {"5v",5000},{"9v",9000},{"12v",12000},{"15v",15000},{"20v",20000},{"28v",28000}
    };
    for (auto &s : shortcuts) {
        if (cmd == s.cmd) {
            uint8_t idx = pd.setVoltage(s.mv, 1000);
            Serial.printf(idx ? "→ PDO %u selected\n" : "No suitable PDO\n", idx);
            if (idx) pd.waitForNegotiation(3000);
            return;
        }
    }

    // set <mV> [mA]
    if (cmd.startsWith("set ")) {
        int spIdx = cmd.indexOf(' ', 4);
        uint16_t mv = (uint16_t)cmd.substring(4, spIdx > 0 ? spIdx : 999).toInt();
        uint16_t ma = spIdx > 0 ? (uint16_t)cmd.substring(spIdx + 1).toInt() : 1000;
        uint8_t idx = pd.setVoltage(mv, ma);
        Serial.printf(idx ? "→ PDO %u selected for %u mV / %u mA\n"
                          : "No PDO found for %u mV / %u mA\n", idx, mv, ma);
        if (idx) {
            String res = pd.getNegotiationResultString(3000);
            Serial.printf("Negotiation: %s\n", res.c_str());
        }
        return;
    }

    // pps <mV> [mA]
    if (cmd.startsWith("pps ")) {
        int spIdx = cmd.indexOf(' ', 4);
        uint16_t mv = (uint16_t)cmd.substring(4, spIdx > 0 ? spIdx : 999).toInt();
        uint16_t ma = spIdx > 0 ? (uint16_t)cmd.substring(spIdx + 1).toInt() : 2000;
        uint8_t idx = pd.setPPS(mv, ma);
        Serial.printf(idx ? "→ PPS PDO %u at %u mV / %u mA\n"
                          : "PPS not available\n", idx, mv, ma);
        return;
    }

    // avs <mV> [mA]
    if (cmd.startsWith("avs ")) {
        int spIdx = cmd.indexOf(' ', 4);
        uint16_t mv = (uint16_t)cmd.substring(4, spIdx > 0 ? spIdx : 999).toInt();
        uint16_t ma = spIdx > 0 ? (uint16_t)cmd.substring(spIdx + 1).toInt() : 3000;
        uint8_t idx = pd.setAVS(mv, ma);
        Serial.printf(idx ? "→ AVS PDO %u at %u mV / %u mA\n"
                          : "AVS not available\n", idx, mv, ma);
        return;
    }

    Serial.printf("Unknown command: '%s'  (type 'help')\n", cmd.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println(F("\n=== AP33772S Full System Demo ==="));
    printHelp();

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    if (pd.begin() != AP33772S_OK) {
        Serial.println(F("[ERR] AP33772S not found on I2C bus."));
        while (true);
    }

    pd.setOVPOffset_mV(2000);
    pd.setUVPThreshold(UVP_80PCT);
    pd.setOCPThreshold_mA(0);        // auto
    pd.setOTPThreshold_C(100);
    pd.setDeratingThreshold_C(80);
    pd.setInterruptMask(MASK_ALL);
    pd.attachInterruptCallback(onInt);

    state = STARTUP;
    Serial.println(F("Waiting for cable..."));
}

// ─────────────────────────────────────────────────────────────────────────────
static String serialBuf;
static uint32_t lastStatusMs = 0;

void loop() {
    pd.task();   // PPS/AVS keep-alive

    // ── State machine ──────────────────────────────────────────────────────
    switch (state) {

    case STARTUP:
        if (pd.waitForStartup(100) == AP33772S_OK) {
            Serial.println(F("Cable attached!"));
            state = SCAN;
        }
        break;

    case SCAN: {
        int8_t r = pd.waitForPDOs(100);
        if (r == AP33772S_OK) {
            pd.readAllPDOs();
            Serial.printf("%u PDO(s) found:\n", pd.getValidPDOCount());
            pd.printPDOs();
            // Auto-request 20 V on startup, fall back gracefully
            uint8_t idx = pd.setVoltage(20000, 1500);
            if (idx == 0) idx = pd.setVoltage(12000, 1000);
            if (idx == 0) idx = pd.set5V(500);
            if (idx > 0 && pd.waitForNegotiation(3000) == AP33772S_OK) {
                Serial.printf("Running at %u mV / %u mA\n",
                              pd.getRequestedVoltage_mV(),
                              pd.getRequestedCurrent_mA());
                state = RUNNING;
            }
        }
        break;
    }

    case RUNNING:
        if (intFired) {
            intFired = false;
            uint8_t cause = pd.clearInterrupt();
            if (cause & STATUS_FAULTS) {
                Serial.printf("[FAULT] %s at V=%u mA=%u T=%d\n",
                              pd.getFaultString().c_str(),
                              pd.getVoltage_mV(), pd.getCurrent_mA(),
                              pd.getTemperature_C());
                state = FAULT;
            } else if (cause & STATUS_NEWPDO) {
                Serial.println(F("[INFO] New PDOs — re-scanning"));
                state = SCAN;
            }
        }
        break;

    case FAULT:
        Serial.println(F("Fault latched. Attempting recovery in 3 s..."));
        delay(3000);
        state = RECOVERY;
        break;

    case RECOVERY: {
        uint16_t targetV = pd.getRequestedVoltage_mV();
        if (targetV == 0) targetV = 5000;
        uint8_t idx = pd.setVoltage(targetV, 1000);
        if (idx && pd.waitForNegotiation(3000) == AP33772S_OK) {
            Serial.println(F("Recovery OK"));
            state = RUNNING;
        } else {
            Serial.println(F("Recovery failed — hard reset"));
            pd.issueHardReset();
            delay(500);
            state = STARTUP;
        }
        break;
    }

    case IDLE: break;
    }

    // ── Serial command processor ───────────────────────────────────────────
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBuf.length()) {
                processCommand(serialBuf);
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }

    // ── Periodic one-line status ───────────────────────────────────────────
    if (state == RUNNING && millis() - lastStatusMs >= 2000) {
        lastStatusMs = millis();
        Serial.printf("V=%5u mV  I=%4u mA  P=%6u mW  T=%3d°C%s\n",
                      pd.getVoltage_mV(), pd.getCurrent_mA(),
                      pd.getPower_mW(),   pd.getTemperature_C(),
                      pd.isDerating() ? "  [DERATE]" : "");
    }
}
