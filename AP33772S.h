/**
 * AP33772S.h  —  Arduino Library for AP33772S USB PD 3.1 Sink Controller
 * ============================================================
 * Datasheet: DS46176 Rev. 9-2, Feb 2026  |  I2C address: 0x52
 * Targets:   RP2040 (Arduino-Pico), ESP32 (Arduino)
 *
 * Based on official CentyLab PicoPD library bit-field definitions.
 * Extended with full EPR/PPS/AVS support, convenience wrappers,
 * interrupt decoding, and datasheet-aligned register names.
 * ============================================================
 */

#ifndef AP33772S_H
#define AP33772S_H

#include <Arduino.h>
#include <Wire.h>

// ── I2C ──────────────────────────────────────────────────────────────────────
#define AP33772S_ADDRESS     (0x52)

// ── Buffer / array sizes ──────────────────────────────────────────────────────
#define MAX_PDO_ENTRIES      (13)
#define SRCPDO_BYTES         (MAX_PDO_ENTRIES * 2)   // 13 PDOs × 2 bytes each

// ── Register addresses (Table 19, DS46176 Rev.9-2) ───────────────────────────
#define CMD_STATUS           (0x01)  // RC — clears on read
#define CMD_MASK             (0x02)
#define CMD_OPMODE           (0x03)
#define CMD_CONFIG           (0x04)
#define CMD_PDCONFIG         (0x05)
#define CMD_SYSTEM           (0x06)
#define CMD_TR25             (0x0C)  // NTC table entry @  25°C (2-byte, Ω)
#define CMD_TR50             (0x0D)  // NTC table entry @  50°C
#define CMD_TR75             (0x0E)  // NTC table entry @  75°C
#define CMD_TR100            (0x0F)  // NTC table entry @ 100°C
#define CMD_VOLTAGE          (0x11)  // VOUT, LSB = 80 mV  (2-byte)
#define CMD_CURRENT          (0x12)  // IOUT, LSB = 24 mA  (1-byte)
#define CMD_TEMP             (0x13)  // NTC temp, unit = °C
#define CMD_VREQ             (0x14)  // Negotiated voltage, LSB = 50 mV (2-byte)
#define CMD_IREQ             (0x15)  // Negotiated current, LSB = 10 mA (2-byte)
#define CMD_VSELMIN          (0x16)  // Min selection voltage, LSB = 200 mV, default 5000 mV
#define CMD_UVPTHR           (0x17)  // UVP threshold (% of VREQ), default 1 = 80%
#define CMD_OVPTHR           (0x18)  // OVP offset from VREQ, LSB = 80 mV, default 2000 mV
#define CMD_OCPTHR           (0x19)  // OCP threshold, LSB = 50 mA, 0 = auto 110% PDO max
#define CMD_OTPTHR           (0x1A)  // OTP threshold, unit = °C, default 120°C
#define CMD_DRTHR            (0x1B)  // Thermal de-rating threshold, unit = °C
#define CMD_SRCPDO           (0x20)  // 26-byte burst: all 13 PDOs
// Individual PDO registers (2-byte each)
#define CMD_SRC_SPR_PDO1     (0x21)
#define CMD_SRC_SPR_PDO7     (0x27)
#define CMD_SRC_EPR_PDO8     (0x28)
#define CMD_SRC_EPR_PDO13    (0x2D)
#define CMD_PD_REQMSG        (0x31)  // RDO write: [15:12]=PDO_INDEX [11:8]=CURRENT_SEL [7:0]=VOLTAGE_SEL
#define CMD_PD_CMDMSG        (0x32)  // bit0=HRST, bit1=DRSWP
#define CMD_PD_MSGRLT        (0x33)  // bit0=SUCCESS

// ── STATUS register bits (0x01, Table 12) ────────────────────────────────────
#define STATUS_STARTED       (1u << 0)
#define STATUS_READY         (1u << 1)
#define STATUS_NEWPDO        (1u << 2)
#define STATUS_UVP           (1u << 3)
#define STATUS_OVP           (1u << 4)
#define STATUS_OCP           (1u << 5)
#define STATUS_OTP           (1u << 6)
#define STATUS_FAULTS        (STATUS_UVP | STATUS_OVP | STATUS_OCP | STATUS_OTP)

// ── MASK register bits (0x02, Table 11) ──────────────────────────────────────
#define MASK_STARTED         (1u << 0)
#define MASK_READY           (1u << 1)
#define MASK_NEWPDO          (1u << 2)
#define MASK_UVP             (1u << 3)
#define MASK_OVP             (1u << 4)
#define MASK_OCP             (1u << 5)
#define MASK_OTP             (1u << 6)
#define MASK_ALL             (0x7Fu)

// ── OPMODE register bits (0x03, Table 13) ────────────────────────────────────
#define OPMODE_LGCYMOD       (1u << 0)   // Legacy (non-PD) source connected
#define OPMODE_PDMOD         (1u << 1)   // PD source connected
#define OPMODE_DR            (1u << 6)   // Thermal de-rating active
#define OPMODE_CCFLIP        (1u << 7)   // CC2 active (cable flipped)

// ── CONFIG register bits (0x04, Table 10) ────────────────────────────────────
#define CONFIG_UVP_EN        (1u << 3)
#define CONFIG_OVP_EN        (1u << 4)
#define CONFIG_OCP_EN        (1u << 5)
#define CONFIG_OTP_EN        (1u << 6)
#define CONFIG_DR_EN         (1u << 7)
#define CONFIG_ALL_EN        (0xF8u)

// ── PDCONFIG register bits (0x05, Table 3) ───────────────────────────────────
#define PDCFG_EPR_EN         (1u << 0)
#define PDCFG_PPS_EN         (1u << 1)
#define PDCFG_DRSWP_EN       (1u << 2)

// ── PD_CMDMSG bits (0x32, Table 18) ─────────────────────────────────────────
#define CMDMSG_HRST          (1u << 0)   // Hard Reset
#define CMDMSG_DRSWP         (1u << 1)   // Data Role Swap

// ── PD_MSGRLT bits (0x33) ────────────────────────────────────────────────────
#define MSGRLT_SUCCESS       (1u << 0)

// ── SYSTEM register values (0x06) ────────────────────────────────────────────
#define SYSTEM_OUTPUT_OFF    (0b00010001)
#define SYSTEM_OUTPUT_ON     (0b00010010)

// ── UVP threshold modes (CMD_UVPTHR = 0x17) ──────────────────────────────────
#define UVP_80PCT            (1)
#define UVP_75PCT            (2)
#define UVP_70PCT            (3)

// ── Cross-platform ISR attribute ──────────────────────────────────────────────
// Use AP33772S_ISR_ATTR on interrupt service routines for portability.
// On ESP32/ESP8266 this places the function in IRAM; on other platforms it is a no-op.
#if defined(ESP32) || defined(ESP8266)
  #define AP33772S_ISR_ATTR IRAM_ATTR
#else
  #define AP33772S_ISR_ATTR
#endif


#define AVS_KEEPALIVE_MS     (500u)

// ── PDO type constants ────────────────────────────────────────────────────────
#define PDO_TYPE_FIXED       (0)
#define PDO_TYPE_PPS         (1)   // type bit=1 in SPR slot (index 1–7)
#define PDO_TYPE_AVS         (2)   // type bit=1 in EPR slot (index 8–13)

// ── Voltage / current hardware limits ────────────────────────────────────────
#define PPS_VMIN_MV          (3300u)
#define PPS_VMAX_MV          (21000u)
#define PPS_VSTEP_MV         (100u)
#define AVS_VMIN_MV          (15000u)
#define AVS_VMAX_MV          (28000u)
#define AVS_VSTEP_MV         (200u)

// ── Return codes ─────────────────────────────────────────────────────────────
#define AP33772S_OK          ( 0)
#define AP33772S_ERR_I2C     (-1)
#define AP33772S_ERR_TIMEOUT (-2)
#define AP33772S_ERR_RANGE   (-3)
#define AP33772S_ERR_TYPE    (-4)
#define AP33772S_ERR_NEGO    (-5)

// ─────────────────────────────────────────────────────────────────────────────
//  16-bit compressed PDO layout (little-endian, Table 2 / CentyLab definition)
//
//  bit  15    : detect       — 1 = slot populated
//  bit  14    : type         — 0 = Fixed,  1 = PPS (SPR) or AVS (EPR)
//  bits 13:10 : current_max  — 4-bit code → 1.0 A … 5.0 A (see currentMap)
//  bits  9:8  : voltage_min  — 2-bit indicator (PPS/AVS) or peak_current (Fixed)
//  bits  7:0  : voltage_max  — 8-bit
//                SPR: voltage_max × 100 mV
//                EPR: voltage_max × 200 mV
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    union {
        struct {
            unsigned int voltage_max  : 8;
            unsigned int peak_current : 2;
            unsigned int current_max  : 4;
            unsigned int type         : 1;
            unsigned int detect       : 1;
        } fixed;
        struct {
            unsigned int voltage_max  : 8;
            unsigned int voltage_min  : 2;
            unsigned int current_max  : 4;
            unsigned int type         : 1;
            unsigned int detect       : 1;
        } pps;
        struct {
            unsigned int voltage_max  : 8;
            unsigned int voltage_min  : 2;
            unsigned int current_max  : 4;
            unsigned int type         : 1;
            unsigned int detect       : 1;
        } avs;
        struct {
            uint8_t byte0;   // LSB
            uint8_t byte1;   // MSB
        };
        uint16_t raw;
    };
} PDO_DATA_T;

// ── Decoded, human-readable PDO ───────────────────────────────────────────────
struct AP33772S_PDO {
    uint8_t  index;          // 1-based (1–13)
    bool     valid;          // detect bit = 1
    bool     isEPR;          // true for index 8–13
    uint8_t  type;           // PDO_TYPE_FIXED / _PPS / _AVS
    uint16_t minVoltage_mV;  // 0 for Fixed; 3300 for PPS; 15000 for AVS
    uint16_t maxVoltage_mV;
    uint16_t maxCurrent_mA;  // Approximate upper bound from currentMap
    uint8_t  currentCode;    // Raw 4-bit current_max field
    uint16_t raw;            // Raw 16-bit register value
};

// ─────────────────────────────────────────────────────────────────────────────
//  Main class
// ─────────────────────────────────────────────────────────────────────────────
class AP33772S {
public:
    explicit AP33772S(TwoWire &wire = Wire, int8_t intPin = -1);

    // ── Initialisation ──────────────────────────────────────────────────────
    int8_t begin(bool enableEPR = true, bool enablePPS = true);
    int8_t waitForStartup(uint32_t timeoutMs = 2000);
    int8_t waitForPDOs(uint32_t timeoutMs = 8000);

    /**
     * MUST be called every loop() iteration when using PPS or AVS.
     * Re-sends the RDO every AVS_KEEPALIVE_MS ms.
     * Without this the charger reverts to 5 V after ~5 s.
     */
    void task();

    // ── PDO discovery ───────────────────────────────────────────────────────
    uint8_t             readAllPDOs();
    bool                readPDO(uint8_t index, AP33772S_PDO &pdo);
    const AP33772S_PDO& getPDO(uint8_t index) const;
    uint8_t             getValidPDOCount() const { return _validPDOCount; }
    int8_t              getPPSIndex()      const;  // First PPS slot, or -1
    int8_t              getAVSIndex()      const;  // First AVS slot, or -1
    bool                hasPPS()           const { return getPPSIndex() >= 0; }
    bool                hasAVS()           const { return getAVSIndex() >= 0; }
    void                printPDOs(Stream &s = Serial);

    // ── Voltage requests — explicit PDO control ──────────────────────────────
    int8_t setFixPDO(uint8_t pdoIndex, uint16_t maxCurrent_mA);
    int8_t setPPSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA);
    int8_t setAVSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA);

    // ── Voltage requests — high-level auto-select ────────────────────────────
    /**
     * Auto-select best PDO and request voltage.
     * Prefers PPS/AVS for exact voltage, falls back to nearest Fixed PDO.
     * @param voltage_mV    Target output voltage in mV
     * @param minCurrent_mA Minimum acceptable PDO current in mA
     * @return selected PDO index (1-13), or 0 on failure
     */
    uint8_t setVoltage(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000);

    /**
     * Like setVoltage() but strictly prefers PPS/AVS → only falls back to
     * Fixed if no PPS/AVS can serve the voltage.
     */
    uint8_t setVoltageExact(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000);

    // Convenience fixed-voltage wrappers
    uint8_t set5V (uint16_t minCurrent_mA = 1000) { return setVoltage( 5000, minCurrent_mA); }
    uint8_t set9V (uint16_t minCurrent_mA = 1000) { return setVoltage( 9000, minCurrent_mA); }
    uint8_t set12V(uint16_t minCurrent_mA = 1000) { return setVoltage(12000, minCurrent_mA); }
    uint8_t set15V(uint16_t minCurrent_mA = 1000) { return setVoltage(15000, minCurrent_mA); }
    uint8_t set20V(uint16_t minCurrent_mA = 1000) { return setVoltage(20000, minCurrent_mA); }
    uint8_t set28V(uint16_t minCurrent_mA = 1000) { return setVoltage(28000, minCurrent_mA); }

    /**
     * Request PPS at given voltage (finds first PPS PDO automatically).
     * Returns PDO index or 0 on failure.
     */
    uint8_t setPPS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000);

    /**
     * Request AVS at given voltage (finds first AVS PDO automatically).
     * Returns PDO index or 0 on failure.
     */
    uint8_t setAVS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000);

    // Find best PPS PDO index for a target voltage (does not issue RDO)
    int8_t findBestPPS(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000) const;
    int8_t findBestAVS(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000) const;

    int8_t  waitForNegotiation(uint32_t timeoutMs = 3000);
    int8_t  issueHardReset();
    bool    issueSoftReset();           // Not in PD spec, sends HRST as fallback
    bool    requestDataRoleSwap();      // Sends DRSWP via PD_CMDMSG

    // ── Output switch ───────────────────────────────────────────────────────
    bool setOutput(bool on);   ///< Drive NMOS VOUT switch via SYSTEM register

    // ── Status ──────────────────────────────────────────────────────────────
    uint8_t getStatus();
    uint8_t getOpMode();
    uint8_t getMsgResult();
    bool    isStarted()         { return (getStatus() & STATUS_STARTED) != 0; }
    bool    isReady()           { return (getStatus() & STATUS_READY)   != 0; }
    bool    hasNewPDO()         { return (getStatus() & STATUS_NEWPDO)  != 0; }
    bool    isPDConnected();
    bool    isLegacyConnected();
    bool    isCableFlipped();
    bool    isDerating();
    bool    isFault();
    String  getFaultString();
    String  getNegotiationResultString(uint32_t timeoutMs = 3000);

    /** Returns a bitmask of STATUS_* bits that were set (reads + clears STATUS). */
    uint8_t getInterruptCause() { return clearInterrupt(); }

    // ── ADC readings ────────────────────────────────────────────────────────
    uint16_t getVoltage_mV();
    uint16_t getCurrent_mA();
    uint32_t getPower_mW();
    int8_t   getTemperature_C();
    uint16_t getRequestedVoltage_mV();
    uint16_t getRequestedCurrent_mA();

    // ── Protection thresholds ────────────────────────────────────────────────
    bool setOVPOffset_mV(uint16_t offset_mV);         // OVP = VREQ + offset (80 mV LSB)
    bool setUVPThreshold(uint8_t uvpMode);             // UVP_80PCT / 75PCT / 70PCT
    bool setOCPThreshold_mA(uint16_t mA);              // 0 = auto (110% of PDO max)
    bool setOTPThreshold_C(uint8_t degC);
    bool setDeratingThreshold_C(uint8_t degC);
    bool setMinVoltage_mV(uint16_t mV);                // VSELMIN — gate for VOUT switch
    bool setProtectionConfig(bool uvp, bool ovp, bool ocp, bool otp, bool dr);

    // ── Interrupts ──────────────────────────────────────────────────────────
    bool    setInterruptMask(uint8_t mask);
    uint8_t clearInterrupt();                          // Returns STATUS then clears it
    void    attachInterruptCallback(void (*cb)());

    // ── NTC thermistor calibration ───────────────────────────────────────────
    // Default values match Murata NCP03XH103 (10kΩ @ 25°C)
    bool setNTC(uint16_t r25 = 10000, uint16_t r50 = 4161,
                uint16_t r75 = 1928,  uint16_t r100 = 974);

    // ── Raw register I/O ────────────────────────────────────────────────────
    int16_t  readReg8(uint8_t reg);          // Returns -1 on I2C error
    int32_t  readReg16(uint8_t reg);         // Returns -1 on I2C error (little-endian)
    bool     writeReg8(uint8_t reg, uint8_t val);
    bool     writeReg16(uint8_t reg, uint16_t val);
    bool     readBytes(uint8_t reg, uint8_t *buf, uint8_t len);
    void     dumpRegisters(Stream &s = Serial);

private:
    TwoWire     &_wire;
    int8_t       _intPin;
    PDO_DATA_T   _pdoRaw[MAX_PDO_ENTRIES];
    AP33772S_PDO _pdoDecoded[MAX_PDO_ENTRIES];
    uint8_t      _validPDOCount;

    // Keep-alive state for PPS/AVS
    bool     _keepAliveActive;
    uint32_t _keepAliveTimer;
    uint8_t  _keepAlivePDOIndex;
    uint16_t _keepAliveVoltageSel;
    uint8_t  _keepAliveCurrentSel;
    bool     _keepAliveIsAVS;

    // Helpers
    void    _decodePDO(uint8_t idx);
    void    _sendRDO(uint8_t pdoIndex, uint8_t currentSel, uint8_t voltageSel);

    // Current lookup table — matches CentyLab currentMap exactly
    // Codes 0x0–0xF → 1.00 A – 5.00 A
    static const uint16_t _currentMap[16];
    uint8_t  _currentEncode(uint16_t mA) const;
    uint16_t _currentDecode(uint8_t code) const;
};

#endif // AP33772S_H
