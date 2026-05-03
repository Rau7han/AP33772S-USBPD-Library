/**
 * AP33772S.cpp — Arduino Library for AP33772S USB PD 3.1 Sink Controller
 * =========================================================================
 * Datasheet: DS46176 Rev. 9-2 (Diodes Incorporated, Feb 2026)
 * I2C address: 0x52  |  Targets: RP2040 (Arduino-Pico), ESP32 (Arduino)
 *
 * Key hardware limits (from datasheet):
 *   PPS  : 3.3 V – 21 V  in 100 mV steps  (SPR PDO slots 1–7, type=1)
 *   AVS  : 15.0 V – 28 V in 200 mV steps  (EPR PDO slots 8–13, type=1)
 *   CURRENT_SEL : codes 0x0–0xF → 1.00 A – 5.00 A (250 mA/step, 5.0 A at 0xF)
 *   VOLTAGE_SEL : for PPS = voltage_mV / 100 ; for AVS = voltage_mV / 200
 *   PDO_INDEX   : bits[15:12] of PD_REQMSG ; 1–7 = SPR, 8–13 = EPR
 *   CURRENT_SEL : bits[11:8]  of PD_REQMSG
 *   VOLTAGE_SEL : bits[ 7:0]  of PD_REQMSG
 * =========================================================================
 */

#include "AP33772S.h"

// ── Current lookup table ──────────────────────────────────────────────────────
// Matches CentyLab PicoPD currentMap: codes 0–14 are 1.0–4.5 A (250 mA/step),
// code 15 = 5.0 A.
const uint16_t AP33772S::_currentMap[16] = {
    1000, 1250, 1500, 1750,
    2000, 2250, 2500, 2750,
    3000, 3250, 3500, 3750,
    4000, 4250, 4500, 5000
};

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
AP33772S::AP33772S(TwoWire &wire, int8_t intPin)
    : _wire(wire), _intPin(intPin), _validPDOCount(0),
      _keepAliveActive(false), _keepAliveTimer(0),
      _keepAlivePDOIndex(0), _keepAliveVoltageSel(0),
      _keepAliveCurrentSel(0), _keepAliveIsAVS(false)
{
    memset(_pdoRaw,     0, sizeof(_pdoRaw));
    memset(_pdoDecoded, 0, sizeof(_pdoDecoded));
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        _pdoDecoded[i].index = i + 1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  begin() — configure PDCONFIG, protections, NTC defaults, interrupt mask
// ─────────────────────────────────────────────────────────────────────────────
int8_t AP33772S::begin(bool enableEPR, bool enablePPS) {
    // Quick I2C sanity check
    _wire.beginTransmission(AP33772S_ADDRESS);
    if (_wire.endTransmission() != 0) return AP33772S_ERR_I2C;

    // PDCONFIG: enable EPR and PPS/AVS as requested (Table 3)
    uint8_t pdcfg = 0;
    if (enableEPR) pdcfg |= PDCFG_EPR_EN;
    if (enablePPS) pdcfg |= PDCFG_PPS_EN;
    writeReg8(CMD_PDCONFIG, pdcfg);

    // CONFIG: enable all protections by default (Table 10)
    writeReg8(CMD_CONFIG, CONFIG_ALL_EN);

    // NTC: load Murata NCP03XH103 default values (Table 17)
    setNTC(10000, 4161, 1928, 974);

    // MASK: enable STARTED and READY interrupts (default per datasheet, Table 11)
    writeReg8(CMD_MASK, MASK_STARTED | MASK_READY | MASK_NEWPDO |
                        MASK_UVP | MASK_OVP | MASK_OCP | MASK_OTP);

    if (_intPin >= 0) {
        pinMode(_intPin, INPUT);
    }
    return AP33772S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  waitForStartup() — poll STATUS.STARTED (cable attachment + AP33772S init)
// ─────────────────────────────────────────────────────────────────────────────
int8_t AP33772S::waitForStartup(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        int16_t s = readReg8(CMD_STATUS);
        if (s < 0) return AP33772S_ERR_I2C;
        if ((uint8_t)s & STATUS_STARTED) return AP33772S_OK;
        delay(10);
    }
    return AP33772S_ERR_TIMEOUT;
}

// ─────────────────────────────────────────────────────────────────────────────
//  waitForPDOs() — poll STATUS.NEWPDO + STATUS.READY
// ─────────────────────────────────────────────────────────────────────────────
int8_t AP33772S::waitForPDOs(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        int16_t s = readReg8(CMD_STATUS);
        if (s < 0) return AP33772S_ERR_I2C;
        uint8_t st = (uint8_t)s;
        if ((st & STATUS_NEWPDO) && (st & STATUS_READY)) return AP33772S_OK;
        delay(20);
    }
    return AP33772S_ERR_TIMEOUT;
}

// ─────────────────────────────────────────────────────────────────────────────
//  task() — keep-alive for PPS / AVS (call every loop() iteration)
// ─────────────────────────────────────────────────────────────────────────────
void AP33772S::task() {
    if (!_keepAliveActive) return;
    if ((millis() - _keepAliveTimer) >= AVS_KEEPALIVE_MS) {
        _keepAliveTimer = millis();
        _sendRDO(_keepAlivePDOIndex, _keepAliveCurrentSel, _keepAliveVoltageSel);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  PDO discovery
// ─────────────────────────────────────────────────────────────────────────────
uint8_t AP33772S::readAllPDOs() {
    uint8_t buf[SRCPDO_BYTES] = {0};
    if (!readBytes(CMD_SRCPDO, buf, SRCPDO_BYTES)) return 0;

    _validPDOCount = 0;
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        _pdoRaw[i].byte0 = buf[i * 2];
        _pdoRaw[i].byte1 = buf[i * 2 + 1];
        _decodePDO(i);
        if (_pdoDecoded[i].valid) _validPDOCount++;
    }
    return _validPDOCount;
}

bool AP33772S::readPDO(uint8_t index, AP33772S_PDO &pdo) {
    if (index < 1 || index > MAX_PDO_ENTRIES) return false;
    uint8_t buf[2];
    uint8_t reg = (index <= 7) ? (CMD_SRC_SPR_PDO1 + index - 1)
                               : (CMD_SRC_EPR_PDO8  + index - 8);
    if (!readBytes(reg, buf, 2)) return false;
    uint8_t idx = index - 1;
    _pdoRaw[idx].byte0 = buf[0];
    _pdoRaw[idx].byte1 = buf[1];
    _decodePDO(idx);
    pdo = _pdoDecoded[idx];
    return true;
}

const AP33772S_PDO& AP33772S::getPDO(uint8_t index) const {
    static AP33772S_PDO empty = {};
    if (index < 1 || index > MAX_PDO_ENTRIES) return empty;
    return _pdoDecoded[index - 1];
}

int8_t AP33772S::getPPSIndex() const {
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        if (_pdoDecoded[i].valid && _pdoDecoded[i].type == PDO_TYPE_PPS)
            return _pdoDecoded[i].index;
    }
    return -1;
}

int8_t AP33772S::getAVSIndex() const {
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        if (_pdoDecoded[i].valid && _pdoDecoded[i].type == PDO_TYPE_AVS)
            return _pdoDecoded[i].index;
    }
    return -1;
}

int8_t AP33772S::findBestPPS(uint16_t voltage_mV, uint16_t minCurrent_mA) const {
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        const AP33772S_PDO &p = _pdoDecoded[i];
        if (!p.valid || p.type != PDO_TYPE_PPS) continue;
        if (voltage_mV < p.minVoltage_mV || voltage_mV > p.maxVoltage_mV) continue;
        if (p.maxCurrent_mA < minCurrent_mA) continue;
        return p.index;
    }
    return -1;
}

int8_t AP33772S::findBestAVS(uint16_t voltage_mV, uint16_t minCurrent_mA) const {
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        const AP33772S_PDO &p = _pdoDecoded[i];
        if (!p.valid || p.type != PDO_TYPE_AVS) continue;
        if (voltage_mV < p.minVoltage_mV || voltage_mV > p.maxVoltage_mV) continue;
        if (p.maxCurrent_mA < minCurrent_mA) continue;
        return p.index;
    }
    return -1;
}

void AP33772S::printPDOs(Stream &s) {
    s.println(F("─────────────────────────────────────────────────────"));
    s.println(F(" #  Type   MinV   MaxV   MaxI   Raw"));
    s.println(F("─────────────────────────────────────────────────────"));
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        const AP33772S_PDO &p = _pdoDecoded[i];
        if (!p.valid) continue;
        const char *typeStr = (p.type == PDO_TYPE_PPS) ? "PPS " :
                              (p.type == PDO_TYPE_AVS) ? "AVS " : "FIX ";
        s.printf(" %2u  %s  %5u  %5u  %5u  0x%04X%s\n",
                 p.index, typeStr,
                 p.minVoltage_mV, p.maxVoltage_mV, p.maxCurrent_mA,
                 p.raw, p.isEPR ? "  [EPR]" : "");
    }
    s.println(F("─────────────────────────────────────────────────────"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Voltage request — explicit PDO control
// ─────────────────────────────────────────────────────────────────────────────
int8_t AP33772S::setFixPDO(uint8_t pdoIndex, uint16_t maxCurrent_mA) {
    if (pdoIndex < 1 || pdoIndex > MAX_PDO_ENTRIES) return AP33772S_ERR_RANGE;
    const AP33772S_PDO &p = _pdoDecoded[pdoIndex - 1];
    if (!p.valid)                   return AP33772S_ERR_RANGE;
    if (p.type != PDO_TYPE_FIXED)   return AP33772S_ERR_TYPE;

    uint8_t cSel = _currentEncode(maxCurrent_mA);
    _sendRDO(pdoIndex, cSel, 0xFF);   // VOLTAGE_SEL = 0xFF = max for Fixed

    _keepAliveActive = false;
    return AP33772S_OK;
}

int8_t AP33772S::setPPSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA) {
    if (pdoIndex < 1 || pdoIndex > 7) return AP33772S_ERR_RANGE;
    const AP33772S_PDO &p = _pdoDecoded[pdoIndex - 1];
    if (!p.valid)                 return AP33772S_ERR_RANGE;
    if (p.type != PDO_TYPE_PPS)   return AP33772S_ERR_TYPE;

    // Clamp and round to nearest 100 mV step
    uint16_t v = voltage_mV;
    if (v < p.minVoltage_mV) v = p.minVoltage_mV;
    if (v > p.maxVoltage_mV) v = p.maxVoltage_mV;
    v = ((v + PPS_VSTEP_MV / 2) / PPS_VSTEP_MV) * PPS_VSTEP_MV;

    uint8_t vSel = (uint8_t)(v / PPS_VSTEP_MV);
    uint8_t cSel = _currentEncode(maxCurrent_mA);
    _sendRDO(pdoIndex, cSel, vSel);

    _keepAliveActive     = true;
    _keepAliveTimer      = millis();
    _keepAlivePDOIndex   = pdoIndex;
    _keepAliveVoltageSel = vSel;
    _keepAliveCurrentSel = cSel;
    _keepAliveIsAVS      = false;
    return AP33772S_OK;
}

int8_t AP33772S::setAVSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA) {
    if (pdoIndex < 8 || pdoIndex > 13) return AP33772S_ERR_RANGE;
    const AP33772S_PDO &p = _pdoDecoded[pdoIndex - 1];
    if (!p.valid)                 return AP33772S_ERR_RANGE;
    if (p.type != PDO_TYPE_AVS)   return AP33772S_ERR_TYPE;

    // Clamp and round to nearest 200 mV step
    uint16_t v = voltage_mV;
    if (v < p.minVoltage_mV) v = p.minVoltage_mV;
    if (v > p.maxVoltage_mV) v = p.maxVoltage_mV;
    v = ((v + AVS_VSTEP_MV / 2) / AVS_VSTEP_MV) * AVS_VSTEP_MV;

    uint8_t vSel = (uint8_t)(v / AVS_VSTEP_MV);
    uint8_t cSel = _currentEncode(maxCurrent_mA);
    _sendRDO(pdoIndex, cSel, vSel);

    _keepAliveActive     = true;
    _keepAliveTimer      = millis();
    _keepAlivePDOIndex   = pdoIndex;
    _keepAliveVoltageSel = vSel;
    _keepAliveCurrentSel = cSel;
    _keepAliveIsAVS      = true;
    return AP33772S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  setVoltage() — auto-select best PDO
//  Priority: PPS/AVS exact match → Fixed exact → Fixed nearest
// ─────────────────────────────────────────────────────────────────────────────
uint8_t AP33772S::setVoltage(uint16_t voltage_mV, uint16_t minCurrent_mA) {
    // 1. Try PPS if voltage in range
    int8_t pi = findBestPPS(voltage_mV, minCurrent_mA);
    if (pi > 0 && setPPSPDO(pi, voltage_mV, minCurrent_mA) == AP33772S_OK)
        return (uint8_t)pi;

    // 2. Try AVS if voltage in range
    int8_t ai = findBestAVS(voltage_mV, minCurrent_mA);
    if (ai > 0 && setAVSPDO(ai, voltage_mV, minCurrent_mA) == AP33772S_OK)
        return (uint8_t)ai;

    // 3. Find nearest Fixed PDO
    uint8_t bestIdx = 0;
    uint16_t bestDelta = 0xFFFF;
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        const AP33772S_PDO &p = _pdoDecoded[i];
        if (!p.valid || p.type != PDO_TYPE_FIXED) continue;
        if (p.maxCurrent_mA < minCurrent_mA) continue;
        uint16_t delta = (p.maxVoltage_mV > voltage_mV)
                        ? p.maxVoltage_mV - voltage_mV
                        : voltage_mV - p.maxVoltage_mV;
        if (delta < bestDelta) { bestDelta = delta; bestIdx = p.index; }
    }
    if (bestIdx > 0) {
        const AP33772S_PDO &bp = _pdoDecoded[bestIdx - 1];
        setFixPDO(bestIdx, bp.maxCurrent_mA);
        return bestIdx;
    }
    return 0;
}

uint8_t AP33772S::setVoltageExact(uint16_t voltage_mV, uint16_t minCurrent_mA) {
    // Try PPS first
    int8_t pi = findBestPPS(voltage_mV, minCurrent_mA);
    if (pi > 0 && setPPSPDO(pi, voltage_mV, minCurrent_mA) == AP33772S_OK)
        return (uint8_t)pi;

    // Try AVS
    int8_t ai = findBestAVS(voltage_mV, minCurrent_mA);
    if (ai > 0 && setAVSPDO(ai, voltage_mV, minCurrent_mA) == AP33772S_OK)
        return (uint8_t)ai;

    // Exact Fixed match only
    for (uint8_t i = 0; i < MAX_PDO_ENTRIES; i++) {
        const AP33772S_PDO &p = _pdoDecoded[i];
        if (!p.valid || p.type != PDO_TYPE_FIXED) continue;
        if (p.maxVoltage_mV != voltage_mV) continue;
        if (p.maxCurrent_mA < minCurrent_mA) continue;
        setFixPDO(p.index, p.maxCurrent_mA);
        return p.index;
    }
    return 0;
}

uint8_t AP33772S::setPPS(uint16_t voltage_mV, uint16_t maxCurrent_mA) {
    int8_t idx = findBestPPS(voltage_mV, 0);
    if (idx < 0) return 0;
    if (setPPSPDO((uint8_t)idx, voltage_mV, maxCurrent_mA) != AP33772S_OK) return 0;
    return (uint8_t)idx;
}

uint8_t AP33772S::setAVS(uint16_t voltage_mV, uint16_t maxCurrent_mA) {
    int8_t idx = findBestAVS(voltage_mV, 0);
    if (idx < 0) return 0;
    if (setAVSPDO((uint8_t)idx, voltage_mV, maxCurrent_mA) != AP33772S_OK) return 0;
    return (uint8_t)idx;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Negotiation control
// ─────────────────────────────────────────────────────────────────────────────
int8_t AP33772S::waitForNegotiation(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        int16_t r = readReg8(CMD_PD_MSGRLT);
        if (r < 0) return AP33772S_ERR_I2C;
        if ((uint8_t)r & MSGRLT_SUCCESS) return AP33772S_OK;
        delay(10);
    }
    return AP33772S_ERR_NEGO;
}

int8_t AP33772S::issueHardReset() {
    _keepAliveActive = false;
    if (!writeReg8(CMD_PD_CMDMSG, CMDMSG_HRST)) return AP33772S_ERR_I2C;
    return AP33772S_OK;
}

bool AP33772S::issueSoftReset() {
    // AP33772S does not have a USB-PD soft reset command; send hard reset instead
    return issueHardReset() == AP33772S_OK;
}

bool AP33772S::requestDataRoleSwap() {
    return writeReg8(CMD_PD_CMDMSG, CMDMSG_DRSWP);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Output switch
// ─────────────────────────────────────────────────────────────────────────────
bool AP33772S::setOutput(bool on) {
    return writeReg8(CMD_SYSTEM, on ? SYSTEM_OUTPUT_ON : SYSTEM_OUTPUT_OFF);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Status queries
// ─────────────────────────────────────────────────────────────────────────────
uint8_t AP33772S::getStatus() {
    int16_t v = readReg8(CMD_STATUS);
    return (v < 0) ? 0 : (uint8_t)v;
}

uint8_t AP33772S::getOpMode() {
    int16_t v = readReg8(CMD_OPMODE);
    return (v < 0) ? 0 : (uint8_t)v;
}

uint8_t AP33772S::getMsgResult() {
    int16_t v = readReg8(CMD_PD_MSGRLT);
    return (v < 0) ? 0 : (uint8_t)v;
}

bool AP33772S::isPDConnected() {
    return (getOpMode() & OPMODE_PDMOD) != 0;
}

bool AP33772S::isLegacyConnected() {
    return (getOpMode() & OPMODE_LGCYMOD) != 0;
}

bool AP33772S::isCableFlipped() {
    return (getOpMode() & OPMODE_CCFLIP) != 0;
}

bool AP33772S::isDerating() {
    return (getOpMode() & OPMODE_DR) != 0;
}

bool AP33772S::isFault() {
    return (getStatus() & STATUS_FAULTS) != 0;
}

String AP33772S::getFaultString() {
    uint8_t st = getStatus();
    if (!(st & STATUS_FAULTS)) return String("OK");
    String s = "FAULT:";
    if (st & STATUS_OVP) s += " OVP";
    if (st & STATUS_UVP) s += " UVP";
    if (st & STATUS_OCP) s += " OCP";
    if (st & STATUS_OTP) s += " OTP";
    return s;
}

String AP33772S::getNegotiationResultString(uint32_t timeoutMs) {
    int8_t r = waitForNegotiation(timeoutMs);
    if (r == AP33772S_OK)       return String("SUCCESS");
    if (r == AP33772S_ERR_NEGO) return String("FAILED (no PS_RDY within timeout)");
    return String("I2C ERROR");
}

// ─────────────────────────────────────────────────────────────────────────────
//  ADC readings
// ─────────────────────────────────────────────────────────────────────────────
uint16_t AP33772S::getVoltage_mV() {
    int32_t v = readReg16(CMD_VOLTAGE);
    return (v < 0) ? 0 : (uint16_t)(v * 80u);   // LSB = 80 mV
}

uint16_t AP33772S::getCurrent_mA() {
    int16_t v = readReg8(CMD_CURRENT);
    return (v < 0) ? 0 : (uint16_t)((uint8_t)v * 24u);  // LSB = 24 mA
}

uint32_t AP33772S::getPower_mW() {
    return (uint32_t)getVoltage_mV() * getCurrent_mA() / 1000u;
}

int8_t AP33772S::getTemperature_C() {
    int16_t v = readReg8(CMD_TEMP);
    return (v < 0) ? 0 : (int8_t)(uint8_t)v;
}

uint16_t AP33772S::getRequestedVoltage_mV() {
    int32_t v = readReg16(CMD_VREQ);
    return (v < 0) ? 0 : (uint16_t)(v * 50u);   // LSB = 50 mV
}

uint16_t AP33772S::getRequestedCurrent_mA() {
    int32_t v = readReg16(CMD_IREQ);
    return (v < 0) ? 0 : (uint16_t)(v * 10u);   // LSB = 10 mA
}

// ─────────────────────────────────────────────────────────────────────────────
//  Protection thresholds
// ─────────────────────────────────────────────────────────────────────────────
bool AP33772S::setOVPOffset_mV(uint16_t offset_mV) {
    // LSB = 80 mV; clamp to 8-bit
    uint16_t raw = (offset_mV + 40u) / 80u;
    if (raw > 0xFF) raw = 0xFF;
    return writeReg8(CMD_OVPTHR, (uint8_t)raw);
}

bool AP33772S::setUVPThreshold(uint8_t uvpMode) {
    if (uvpMode < 1 || uvpMode > 3) return false;
    return writeReg8(CMD_UVPTHR, uvpMode);
}

bool AP33772S::setOCPThreshold_mA(uint16_t mA) {
    // LSB = 50 mA; 0 = auto (110% PDO max)
    uint16_t raw = (mA == 0) ? 0 : ((mA + 25u) / 50u);
    if (raw > 0xFF) raw = 0xFF;
    return writeReg8(CMD_OCPTHR, (uint8_t)raw);
}

bool AP33772S::setOTPThreshold_C(uint8_t degC) {
    return writeReg8(CMD_OTPTHR, degC);
}

bool AP33772S::setDeratingThreshold_C(uint8_t degC) {
    return writeReg8(CMD_DRTHR, degC);
}

bool AP33772S::setMinVoltage_mV(uint16_t mV) {
    // LSB = 200 mV; default = 5000 mV (0x19)
    uint8_t raw = (uint8_t)((mV + 100u) / 200u);
    return writeReg8(CMD_VSELMIN, raw);
}

bool AP33772S::setProtectionConfig(bool uvp, bool ovp, bool ocp, bool otp, bool dr) {
    uint8_t cfg = 0;
    if (uvp) cfg |= CONFIG_UVP_EN;
    if (ovp) cfg |= CONFIG_OVP_EN;
    if (ocp) cfg |= CONFIG_OCP_EN;
    if (otp) cfg |= CONFIG_OTP_EN;
    if (dr)  cfg |= CONFIG_DR_EN;
    return writeReg8(CMD_CONFIG, cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Interrupts
// ─────────────────────────────────────────────────────────────────────────────
bool AP33772S::setInterruptMask(uint8_t mask) {
    return writeReg8(CMD_MASK, mask & MASK_ALL);
}

uint8_t AP33772S::clearInterrupt() {
    // STATUS is Read-Clear — reading it clears all bits
    int16_t v = readReg8(CMD_STATUS);
    return (v < 0) ? 0 : (uint8_t)v;
}

void AP33772S::attachInterruptCallback(void (*cb)()) {
    if (_intPin < 0 || !cb) return;
    // INT pin is open-drain, active-low — trigger on the falling edge
    attachInterrupt(digitalPinToInterrupt(_intPin), cb, FALLING);
}

// ─────────────────────────────────────────────────────────────────────────────
//  NTC calibration
// ─────────────────────────────────────────────────────────────────────────────
bool AP33772S::setNTC(uint16_t r25, uint16_t r50, uint16_t r75, uint16_t r100) {
    bool ok = true;
    ok &= writeReg16(CMD_TR25,  r25);
    ok &= writeReg16(CMD_TR50,  r50);
    ok &= writeReg16(CMD_TR75,  r75);
    ok &= writeReg16(CMD_TR100, r100);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Raw register I/O
// ─────────────────────────────────────────────────────────────────────────────
int16_t AP33772S::readReg8(uint8_t reg) {
    _wire.beginTransmission(AP33772S_ADDRESS);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return -1;
    if (_wire.requestFrom(AP33772S_ADDRESS, (uint8_t)1) != 1) return -1;
    return (int16_t)_wire.read();
}

int32_t AP33772S::readReg16(uint8_t reg) {
    uint8_t buf[2];
    if (!readBytes(reg, buf, 2)) return -1;
    // Little-endian: buf[0]=LSB, buf[1]=MSB
    return (int32_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

bool AP33772S::writeReg8(uint8_t reg, uint8_t val) {
    _wire.beginTransmission(AP33772S_ADDRESS);
    _wire.write(reg);
    _wire.write(val);
    return _wire.endTransmission() == 0;
}

bool AP33772S::writeReg16(uint8_t reg, uint16_t val) {
    _wire.beginTransmission(AP33772S_ADDRESS);
    _wire.write(reg);
    _wire.write((uint8_t)(val & 0xFF));        // LSB first (little-endian)
    _wire.write((uint8_t)((val >> 8) & 0xFF)); // MSB
    return _wire.endTransmission() == 0;
}

bool AP33772S::readBytes(uint8_t reg, uint8_t *buf, uint8_t len) {
    _wire.beginTransmission(AP33772S_ADDRESS);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return false;
    uint8_t got = _wire.requestFrom(AP33772S_ADDRESS, len);
    if (got != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = _wire.read();
    return true;
}

void AP33772S::dumpRegisters(Stream &s) {
    const uint8_t regs[] = {
        CMD_STATUS, CMD_MASK, CMD_OPMODE, CMD_CONFIG, CMD_PDCONFIG, CMD_SYSTEM,
        CMD_VOLTAGE, CMD_CURRENT, CMD_TEMP, CMD_VREQ, CMD_IREQ, CMD_VSELMIN,
        CMD_UVPTHR, CMD_OVPTHR, CMD_OCPTHR, CMD_OTPTHR, CMD_DRTHR,
        CMD_PD_REQMSG, CMD_PD_CMDMSG, CMD_PD_MSGRLT
    };
    const char *names[] = {
        "STATUS","MASK","OPMODE","CONFIG","PDCONFIG","SYSTEM",
        "VOLTAGE","CURRENT","TEMP","VREQ","IREQ","VSELMIN",
        "UVPTHR","OVPTHR","OCPTHR","OTPTHR","DRTHR",
        "PD_REQMSG","PD_CMDMSG","PD_MSGRLT"
    };
    s.println(F("── AP33772S Register Dump ──────────────────────────"));
    for (uint8_t i = 0; i < sizeof(regs); i++) {
        int16_t v = readReg8(regs[i]);
        s.printf("  0x%02X  %-12s  0x%02X  (%3d)\n",
                 regs[i], names[i], (uint8_t)v, (uint8_t)v);
    }
    s.println(F("────────────────────────────────────────────────────"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * _decodePDO() — convert raw PDO_DATA_T at index idx into AP33772S_PDO.
 *
 * SPR slots (idx 0–6): voltage_max × 100 mV
 * EPR slots (idx 7–12): voltage_max × 200 mV
 *
 * 16-bit wire format (little-endian):
 *   bit 15   : detect
 *   bit 14   : type  (0=Fixed, 1=PPS for SPR / AVS for EPR)
 *   bits 13:10: current_max code
 *   bits  9:8 : voltage_min indicator / peak_current
 *   bits  7:0 : voltage_max
 */
void AP33772S::_decodePDO(uint8_t idx) {
    AP33772S_PDO &d  = _pdoDecoded[idx];
    const PDO_DATA_T &r = _pdoRaw[idx];

    d.index = idx + 1;
    d.isEPR = (idx >= 7);
    d.raw   = r.raw;

    // detect bit must be set for a valid slot
    d.valid = (r.pps.detect != 0);
    if (!d.valid) {
        d.type = PDO_TYPE_FIXED;
        d.minVoltage_mV = d.maxVoltage_mV = d.maxCurrent_mA = 0;
        d.currentCode = 0;
        return;
    }

    d.currentCode   = r.pps.current_max;
    d.maxCurrent_mA = _currentDecode(d.currentCode);

    if (!d.isEPR) {
        // SPR slot
        if (r.pps.type == 0) {
            // Fixed PDO: max voltage in 100 mV units
            d.type          = PDO_TYPE_FIXED;
            d.minVoltage_mV = 0;
            d.maxVoltage_mV = (uint16_t)r.fixed.voltage_max * 100u;
        } else {
            // PPS PDO: voltage range 3.3 V – max
            d.type          = PDO_TYPE_PPS;
            d.minVoltage_mV = PPS_VMIN_MV;
            d.maxVoltage_mV = (uint16_t)r.pps.voltage_max * 100u;
            if (d.maxVoltage_mV > PPS_VMAX_MV) d.maxVoltage_mV = PPS_VMAX_MV;
        }
    } else {
        // EPR slot — always AVS if valid
        d.type          = PDO_TYPE_AVS;
        d.minVoltage_mV = AVS_VMIN_MV;
        d.maxVoltage_mV = (uint16_t)r.avs.voltage_max * 200u;
        if (d.maxVoltage_mV > AVS_VMAX_MV) d.maxVoltage_mV = AVS_VMAX_MV;
    }
}

/**
 * _sendRDO() — write the 16-bit PD_REQMSG register.
 *
 * PD_REQMSG layout (Table 2):
 *   bits [15:12] = PDO_INDEX  (1–7 for SPR, 8–13 for EPR)
 *   bits [11: 8] = CURRENT_SEL
 *   bits [ 7: 0] = VOLTAGE_SEL (0xFF = max for Fixed; raw units for PPS/AVS)
 */
void AP33772S::_sendRDO(uint8_t pdoIndex, uint8_t currentSel, uint8_t voltageSel) {
    uint16_t msg = ((uint16_t)(pdoIndex  & 0x0F) << 12)
                 | ((uint16_t)(currentSel & 0x0F) << 8)
                 | ((uint16_t) voltageSel);
    writeReg16(CMD_PD_REQMSG, msg);
}

/**
 * _currentEncode() — find the CURRENT_SEL code (0–15) closest to mA.
 * Rounds to the nearest entry and clamps to [0, 15].
 */
uint8_t AP33772S::_currentEncode(uint16_t mA) const {
    uint8_t best = 0;
    uint16_t bestDelta = 0xFFFF;
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t d = (_currentMap[i] > mA) ? _currentMap[i] - mA
                                           : mA - _currentMap[i];
        if (d < bestDelta) { bestDelta = d; best = i; }
        if (d == 0) break;
    }
    return best;
}

/**
 * _currentDecode() — convert a 4-bit CURRENT_SEL code to mA.
 */
uint16_t AP33772S::_currentDecode(uint8_t code) const {
    return _currentMap[code & 0x0F];
}
