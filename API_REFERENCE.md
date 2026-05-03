# AP33772S API Reference

Complete API documentation for the AP33772S Arduino library.

---

## Table of Contents

- [Constructor](#constructor)
- [Initialization](#initialization)
- [PDO Discovery](#pdo-discovery)
- [Voltage Control](#voltage-control)
- [Negotiation & Reset](#negotiation--reset)
- [Output Switch](#output-switch)
- [Status Queries](#status-queries)
- [ADC Readings](#adc-readings)
- [Protection Thresholds](#protection-thresholds)
- [Interrupt Control](#interrupt-control)
- [NTC Thermistor Calibration](#ntc-thermistor-calibration)
- [Raw Register I/O](#raw-register-io)
- [Data Types](#data-types)
- [Constants](#constants)
- [Return Codes](#return-codes)

---

## Constructor

```cpp
AP33772S(TwoWire &wire = Wire, int8_t intPin = -1)
```

| Parameter | Description |
|-----------|-------------|
| `wire` | I²C bus instance (default: `Wire`) |
| `intPin` | GPIO pin connected to AP33772S INT output; pass `-1` to disable interrupt support |

**Example:**
```cpp
AP33772S pd(Wire, 6);      // INT on GPIO 6
AP33772S pd(Wire, -1);     // No interrupt
AP33772S pd(Wire1, 6);     // Secondary I2C bus
```

---

## Initialization

### `begin()`
```cpp
int8_t begin(bool enableEPR = true, bool enablePPS = true)
```
Configures the AP33772S: sets PDCONFIG, enables all protections, loads default NTC values, and arms the interrupt mask. Must be called after `Wire.begin()`.

| Parameter | Description |
|-----------|-------------|
| `enableEPR` | Enable Extended Power Range (EPR) up to 28 V |
| `enablePPS` | Enable Programmable Power Supply (PPS) and AVS modes |

Returns `AP33772S_OK` or `AP33772S_ERR_I2C`.

---

### `waitForStartup()`
```cpp
int8_t waitForStartup(uint32_t timeoutMs = 2000)
```
Blocks until `STATUS.STARTED` is set (cable attachment + AP33772S internal init complete).

Returns `AP33772S_OK` or `AP33772S_ERR_TIMEOUT`.

---

### `waitForPDOs()`
```cpp
int8_t waitForPDOs(uint32_t timeoutMs = 8000)
```
Blocks until `STATUS.NEWPDO` and `STATUS.READY` are both set (charger capabilities received).

Returns `AP33772S_OK` or `AP33772S_ERR_TIMEOUT`.

---

### `task()`
```cpp
void task()
```
**Must be called every `loop()` iteration** when using PPS or AVS mode. Re-sends the active RDO every `AVS_KEEPALIVE_MS` ms (default 500 ms). Without this the charger reverts to 5 V after approximately 5 seconds (USB-PD specification requires periodic RDO refresh).

---

## PDO Discovery

### `readAllPDOs()`
```cpp
uint8_t readAllPDOs()
```
Reads all 13 PDO slots from the AP33772S in a single 26-byte burst and caches the decoded results internally.

Returns the count of valid (populated) PDO slots.

---

### `readPDO()`
```cpp
bool readPDO(uint8_t index, AP33772S_PDO &pdo)
```
Reads and decodes a single PDO by 1-based index (1–13).

Returns `true` on success.

---

### `getPDO()`
```cpp
const AP33772S_PDO& getPDO(uint8_t index) const
```
Returns a reference to the cached decoded PDO at 1-based `index`. Call `readAllPDOs()` first.

---

### `getValidPDOCount()`
```cpp
uint8_t getValidPDOCount() const
```
Returns the number of valid PDO slots from the last `readAllPDOs()` call.

---

### `getPPSIndex()` / `getAVSIndex()`
```cpp
int8_t getPPSIndex() const
int8_t getAVSIndex() const
```
Returns the 1-based index of the first PPS or AVS slot respectively, or `-1` if none available.

---

### `hasPPS()` / `hasAVS()`
```cpp
bool hasPPS() const
bool hasAVS() const
```
Returns `true` if the connected charger advertises at least one PPS or AVS PDO.

---

### `findBestPPS()` / `findBestAVS()`
```cpp
int8_t findBestPPS(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000) const
int8_t findBestAVS(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000) const
```
Searches cached PDOs for a PPS/AVS slot that covers `voltage_mV` and meets `minCurrent_mA`. Does **not** issue an RDO.

Returns the 1-based PDO index, or `-1` if not found.

---

### `printPDOs()`
```cpp
void printPDOs(Stream &s = Serial)
```
Prints a formatted table of all valid PDOs to `s`.

---

## Voltage Control

### `setVoltage()` — auto-select
```cpp
uint8_t setVoltage(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000)
```
Auto-selects the best PDO and issues an RDO. Selection priority:
1. PPS — if `voltage_mV` falls within a PPS range
2. AVS — if `voltage_mV` falls within an AVS range
3. Nearest Fixed PDO (by voltage)

| Parameter | Description |
|-----------|-------------|
| `voltage_mV` | Target output voltage in mV |
| `minCurrent_mA` | Minimum acceptable PDO current capability |

Returns the selected PDO index (1–13) or `0` on failure.

---

### `setVoltageExact()`
```cpp
uint8_t setVoltageExact(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000)
```
Like `setVoltage()` but only falls back to a Fixed PDO if its voltage **exactly** matches `voltage_mV`.

---

### Convenience Wrappers
```cpp
uint8_t set5V (uint16_t minCurrent_mA = 1000)
uint8_t set9V (uint16_t minCurrent_mA = 1000)
uint8_t set12V(uint16_t minCurrent_mA = 1000)
uint8_t set15V(uint16_t minCurrent_mA = 1000)
uint8_t set20V(uint16_t minCurrent_mA = 1000)
uint8_t set28V(uint16_t minCurrent_mA = 1000)
```
Shorthand for `setVoltage(N, minCurrent_mA)` at standard fixed voltages.

---

### `setPPS()` / `setAVS()` — auto-find and request
```cpp
uint8_t setPPS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000)
uint8_t setAVS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000)
```
Finds the first PPS/AVS PDO automatically and issues an RDO at `voltage_mV`. Enables keep-alive.

Returns the selected PDO index or `0` on failure.

---

### `setFixPDO()` — explicit Fixed PDO
```cpp
int8_t setFixPDO(uint8_t pdoIndex, uint16_t maxCurrent_mA)
```
Issues an RDO for a specific Fixed PDO. Disables keep-alive.

Returns `AP33772S_OK` or an error code.

---

### `setPPSPDO()` — explicit PPS PDO
```cpp
int8_t setPPSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA)
```
Issues an RDO at a specific voltage within the specified PPS slot (index 1–7). Voltage is clamped to the PDO range and rounded to the nearest 100 mV step. Enables keep-alive.

Returns `AP33772S_OK` or an error code.

---

### `setAVSPDO()` — explicit AVS PDO
```cpp
int8_t setAVSPDO(uint8_t pdoIndex, uint16_t voltage_mV, uint16_t maxCurrent_mA)
```
Issues an RDO at a specific voltage within the specified AVS slot (index 8–13). Voltage is clamped and rounded to the nearest 200 mV step. Enables keep-alive.

Returns `AP33772S_OK` or an error code.

---

## Negotiation & Reset

### `waitForNegotiation()`
```cpp
int8_t waitForNegotiation(uint32_t timeoutMs = 3000)
```
Blocks until `PD_MSGRLT.SUCCESS` is set (PS_RDY received from charger).

Returns `AP33772S_OK`, `AP33772S_ERR_NEGO`, or `AP33772S_ERR_I2C`.

---

### `getNegotiationResultString()`
```cpp
String getNegotiationResultString(uint32_t timeoutMs = 3000)
```
Waits for negotiation and returns a human-readable result string: `"SUCCESS"`, `"FAILED ..."`, or `"I2C ERROR"`.

---

### `issueHardReset()`
```cpp
int8_t issueHardReset()
```
Sends a USB-PD Hard Reset. Disables keep-alive. The charger will re-advertise capabilities after the reset.

Returns `AP33772S_OK` or `AP33772S_ERR_I2C`.

---

### `issueSoftReset()`
```cpp
bool issueSoftReset()
```
The AP33772S does not implement a PD Soft Reset; this calls `issueHardReset()` as a fallback.

---

### `requestDataRoleSwap()`
```cpp
bool requestDataRoleSwap()
```
Sends a Data Role Swap (DR_SWAP) request via `PD_CMDMSG`.

---

## Output Switch

### `setOutput()`
```cpp
bool setOutput(bool on)
```
Controls the external NMOS VOUT switch via the SYSTEM register.

| Value | Action |
|-------|--------|
| `true` | Enable VOUT (write `0x12`) |
| `false` | Disable VOUT (write `0x11`) |

---

## Status Queries

```cpp
uint8_t getStatus()        // Raw STATUS register (0x01, RC)
uint8_t getOpMode()        // Raw OPMODE register (0x03)
uint8_t getMsgResult()     // Raw PD_MSGRLT register (0x33)
bool    isStarted()        // STATUS.STARTED bit
bool    isReady()          // STATUS.READY bit
bool    hasNewPDO()        // STATUS.NEWPDO bit
bool    isPDConnected()    // OPMODE.PDMOD bit
bool    isLegacyConnected()// OPMODE.LGCYMOD bit
bool    isCableFlipped()   // OPMODE.CCFLIP bit (CC2 active)
bool    isDerating()       // OPMODE.DR bit (thermal de-rating active)
bool    isFault()          // Any of UVP/OVP/OCP/OTP set in STATUS
String  getFaultString()   // Human-readable fault description or "OK"
uint8_t getInterruptCause()// Reads and clears STATUS; returns bitmask
```

---

## ADC Readings

```cpp
uint16_t getVoltage_mV()          // VOUT, LSB = 80 mV
uint16_t getCurrent_mA()          // IOUT, LSB = 24 mA
uint32_t getPower_mW()            // Computed: VOUT × IOUT / 1000
int8_t   getTemperature_C()       // NTC temperature in °C
uint16_t getRequestedVoltage_mV() // Negotiated VREQ, LSB = 50 mV
uint16_t getRequestedCurrent_mA() // Negotiated IREQ, LSB = 10 mA
```

---

## Protection Thresholds

### `setOVPOffset_mV()`
```cpp
bool setOVPOffset_mV(uint16_t offset_mV)
```
Sets the Overvoltage Protection threshold as an offset above VREQ (LSB = 80 mV). Default: 2000 mV.

---

### `setUVPThreshold()`
```cpp
bool setUVPThreshold(uint8_t uvpMode)
```
Sets the Undervoltage Protection threshold as a percentage of VREQ.

| Constant | Threshold |
|----------|-----------|
| `UVP_80PCT` (1) | 80% of VREQ |
| `UVP_75PCT` (2) | 75% of VREQ |
| `UVP_70PCT` (3) | 70% of VREQ |

---

### `setOCPThreshold_mA()`
```cpp
bool setOCPThreshold_mA(uint16_t mA)
```
Sets the Overcurrent Protection threshold (LSB = 50 mA). Pass `0` to use automatic mode (110% of negotiated PDO max current).

---

### `setOTPThreshold_C()`
```cpp
bool setOTPThreshold_C(uint8_t degC)
```
Sets the Overtemperature Protection threshold in °C. Default: 120°C.

---

### `setDeratingThreshold_C()`
```cpp
bool setDeratingThreshold_C(uint8_t degC)
```
Sets the thermal de-rating start threshold in °C. Above this temperature the AP33772S automatically requests less current.

---

### `setMinVoltage_mV()`
```cpp
bool setMinVoltage_mV(uint16_t mV)
```
Sets the minimum negotiated voltage required before the VOUT switch is enabled (VSELMIN register, LSB = 200 mV). Default: 5000 mV.

---

### `setProtectionConfig()`
```cpp
bool setProtectionConfig(bool uvp, bool ovp, bool ocp, bool otp, bool dr)
```
Enables or disables individual protection features via the CONFIG register.

---

## Interrupt Control

### `setInterruptMask()`
```cpp
bool setInterruptMask(uint8_t mask)
```
Configures which STATUS bits generate an INT signal. Use `MASK_*` constants or `MASK_ALL`.

---

### `clearInterrupt()`
```cpp
uint8_t clearInterrupt()
```
Reads the STATUS register (which clears it — RC) and returns the bitmask of set bits.

---

### `attachInterruptCallback()`
```cpp
void attachInterruptCallback(void (*cb)())
```
Attaches an ISR to the INT pin (configured at construction). The INT pin is active-low; the callback fires on the falling edge.

Use `AP33772S_ISR_ATTR` on the ISR for cross-platform compatibility:
```cpp
void AP33772S_ISR_ATTR myISR() {
    intFired = true;
}
pd.attachInterruptCallback(myISR);
```

---

## NTC Thermistor Calibration

### `setNTC()`
```cpp
bool setNTC(uint16_t r25 = 10000, uint16_t r50 = 4161,
            uint16_t r75 = 1928,  uint16_t r100 = 974)
```
Writes NTC thermistor resistance values (Ω) at 25, 50, 75, and 100 °C to the AP33772S calibration registers. Default values match the **Murata NCP03XH103** (10 kΩ @ 25°C).

---

## Raw Register I/O

```cpp
int16_t  readReg8(uint8_t reg)           // Returns byte value, or -1 on I2C error
int32_t  readReg16(uint8_t reg)          // Returns 16-bit LE value, or -1 on error
bool     writeReg8(uint8_t reg, uint8_t val)
bool     writeReg16(uint8_t reg, uint16_t val)
bool     readBytes(uint8_t reg, uint8_t *buf, uint8_t len)
void     dumpRegisters(Stream &s = Serial)
```

---

## Data Types

### `AP33772S_PDO`
Decoded, human-readable PDO information.

| Field | Type | Description |
|-------|------|-------------|
| `index` | `uint8_t` | 1-based slot index (1–13) |
| `valid` | `bool` | `true` if slot is populated |
| `isEPR` | `bool` | `true` for EPR slots (8–13) |
| `type` | `uint8_t` | `PDO_TYPE_FIXED`, `PDO_TYPE_PPS`, or `PDO_TYPE_AVS` |
| `minVoltage_mV` | `uint16_t` | Minimum voltage (0 for Fixed) |
| `maxVoltage_mV` | `uint16_t` | Maximum voltage |
| `maxCurrent_mA` | `uint16_t` | Maximum current from lookup table |
| `currentCode` | `uint8_t` | Raw 4-bit current_max field |
| `raw` | `uint16_t` | Raw 16-bit register value |

---

## Constants

### PDO Types
| Constant | Value | Description |
|----------|-------|-------------|
| `PDO_TYPE_FIXED` | 0 | Fixed voltage PDO |
| `PDO_TYPE_PPS` | 1 | Programmable Power Supply (SPR slots 1–7) |
| `PDO_TYPE_AVS` | 2 | Adjustable Voltage Supply (EPR slots 8–13) |

### Voltage / Current Limits
| Constant | Value |
|----------|-------|
| `PPS_VMIN_MV` | 3300 mV |
| `PPS_VMAX_MV` | 21000 mV |
| `PPS_VSTEP_MV` | 100 mV |
| `AVS_VMIN_MV` | 15000 mV |
| `AVS_VMAX_MV` | 28000 mV |
| `AVS_VSTEP_MV` | 200 mV |
| `AVS_KEEPALIVE_MS` | 500 ms |

### UVP Modes
| Constant | Value |
|----------|-------|
| `UVP_80PCT` | 1 |
| `UVP_75PCT` | 2 |
| `UVP_70PCT` | 3 |

### I2C
| Constant | Value |
|----------|-------|
| `AP33772S_ADDRESS` | `0x52` |

---

## Return Codes

| Constant | Value | Meaning |
|----------|-------|---------|
| `AP33772S_OK` | 0 | Success |
| `AP33772S_ERR_I2C` | -1 | I2C communication failure |
| `AP33772S_ERR_TIMEOUT` | -2 | Operation timed out |
| `AP33772S_ERR_RANGE` | -3 | PDO index or parameter out of range |
| `AP33772S_ERR_TYPE` | -4 | PDO type mismatch |
| `AP33772S_ERR_NEGO` | -5 | PD negotiation failed |
