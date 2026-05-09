# AP33772S Arduino Library

**Full USB Power Delivery 3.1 Sink Controller Library for RP2040 & ESP32**

![Status](https://img.shields.io/badge/status-stable-brightgreen) ![License](https://img.shields.io/badge/license-MIT-blue) ![Arduino](https://img.shields.io/badge/Arduino-compatible-success)

## Overview

This Arduino library provides **complete control** over the **Diodes AP33772S** USB PD 3.1 sink controller via I2C. It supports all modern USB Power Delivery features including Extended Power Range (EPR), Programmable Power Supply (PPS), and Adjustable Voltage Supply (AVS).

**Perfect for:**
- USB-C power delivery development boards
- Programmable power supplies and chargers
- Battery management systems with USB-PD charging
- Multi-voltage embedded projects
- Power converter and buck module controllers

---

## Features

### ✨ Core Capabilities
- **Full USB PD 3.1 support** with EPR up to **28V**
- **PPS Mode**: 3.3V – 21V in **100 mV steps** (SPR PDO slots 1–7)
- **AVS Mode**: 15V – 28V in **200 mV steps** (EPR PDO slots 8–13)
- **Fixed PDO support**: Standard 5V/9V/12V/15V/20V negotiation
- **Automatic keep-alive**: 500ms RDO refresh for PPS/AVS stability
- **Multi-platform**: RP2040 (Arduino-Pico), ESP32 (Arduino), and compatible boards

### 🛡️ Protection Features
- **Overvoltage Protection (OVP)**: Configurable offset (80 mV LSB)
- **Undervoltage Protection (UVP)**: 70%, 75%, or 80% threshold modes
- **Overcurrent Protection (OCP)**: Manual or auto-110% PDO max
- **Overtemperature Protection (OTP)**: Configurable threshold
- **Thermal De-rating**: Automatic power limit above threshold
- **Fault detection & recovery**: Interrupt-driven with automatic reset

### 📊 Monitoring & Diagnostics
- **Real-time ADC readings**: Voltage (80 mV LSB), current (24 mA LSB)
- **Power calculation**: Automatic mW computation
- **NTC thermistor support**: Calibrated to Murata NCP03XH103 (10kΩ @ 25°C)
- **Requested parameters**: Negotiated VREQ, IREQ from charger
- **Cable orientation**: CC flip detection
- **Derating status**: Thermal throttling indicator

### 🎮 User-Friendly API
- High-level voltage request: `setVoltage(12000, 2000)` → auto-selects best PDO
- Low-level control: Explicit `setPPSPDO()`, `setAVSPDO()`, `setFixPDO()`
- PDO discovery: Read and cache all charger capabilities
- Convenience methods: `set5V()`, `set9V()`, `set12V()`, etc.
- Smart fallback logic: PPS/AVS preferred, falls back to Fixed PDO

### ⚡ I2C Interface
- **Address**: 0x52 (standard, non-configurable)
- **Clock**: 400 kHz (standard Arduino I2C)
- **Interrupt support**: Optional INT pin for event-driven architecture

---

## Quick Start

### 1. Installation

**Option A: Arduino IDE Library Manager (Recommended)**
1. Open Arduino IDE
2. Go to **Sketch** → **Include Library** → **Manage Libraries**
3. Search for **"AP33772S"**
4. Click **Install**

**Option B: Manual Installation**
1. Download this repository as ZIP
2. Extract to `~/Arduino/libraries/AP33772S`
3. Restart Arduino IDE

### 2. Hardware Setup

**PicoPD Pro (RP2040) — Pin Defaults:**
```
AP33772S         PicoPD Pro (RP2040)
    SDA    →    GPIO 20 (GP20)
    SCL    →    GPIO 21 (GP21)
    INT    →    GPIO  6 (GP6)   [optional]
    GND    →    GND
    VCC    →    3.3V
```

**ESP32 — Standard I2C:**
```
AP33772S         ESP32
    SDA    →    GPIO 21 (default I2C SDA)
    SCL    →    GPIO 22 (default I2C SCL)
    INT    →    GPIO  XX (optional, configure as needed)
    GND    →    GND
    VCC    →    3.3V
```

### 3. Basic Example

```cpp
#include <Wire.h>
#include "AP33772S.h"

AP33772S pd(Wire, -1);  // No interrupt pin

void setup() {
    Serial.begin(115200);
    Wire.begin(20, 21);        // SDA, SCL (RP2040)
    Wire.setClock(400000);

    if (pd.begin(true, true) != AP33772S_OK) {
        Serial.println("AP33772S not found!");
        while (1);
    }

    pd.waitForStartup(10000);
    pd.waitForPDOs(10000);
    pd.readAllPDOs();

    // Request 12V @ 2A (auto-selects best available PDO)
    uint8_t idx = pd.setVoltage(12000, 2000);
    if (idx) pd.waitForNegotiation(3000);
}

void loop() {
    pd.task();  // ← MUST call every loop() for PPS/AVS keep-alive

    Serial.printf("V=%u mV  I=%u mA  P=%u mW  T=%d°C\n",
                  pd.getVoltage_mV(), pd.getCurrent_mA(),
                  pd.getPower_mW(),   pd.getTemperature_C());
    delay(1000);
}
```

---

## Examples

### [01_ScanPDOs](examples/01_ScanPDOs/)
Discover all charger capabilities and display them in a formatted table.

### [02_RequestVoltage](examples/02_RequestVoltage/)
Auto-negotiate a specific voltage with live power measurements.

### [03_PPSVariable](examples/03_PPSVariable/)
Sweep output voltage smoothly using PPS/AVS capabilities. Demonstrates keep-alive requirement.

### [04_ProtectionMonitor](examples/04_ProtectionMonitor/)
Interrupt-driven fault detection with automatic recovery and customizable protection thresholds.

### [05_FullSystem](examples/05_FullSystem/)
Complete state machine with Serial command interface (scan, set voltage, dump registers, etc.).

### [06_SparkAnalyzer](examples/06_SparkAnalyzer/) ⭐ ESP32 only
**Wi-Fi and BLE enabled USB-C PD Analyzer & Programmable Power Supply** firmware for the Spark Analyzer device.

Features:
- **Wi-Fi** (STA mode with AP fallback for provisioning) + mDNS (`http://spark.local/`)
- **Live web dashboard** served directly from ESP32 — real-time voltage/current/power charts and controls
- **REST API** for remote monitoring and control (see [API_REFERENCE.md](SparkAnalyzer/documentation/API_REFERENCE.md))
- **BLE GATT server** for mobile app connectivity (500 ms notify interval)
- **PPS/AVS voltage control** from the dashboard: 3.3 V – 21 V in 100 mV steps
- **Interrupt-driven fault detection** with automatic recovery

See the [Spark Analyzer documentation](SparkAnalyzer/documentation/) for full setup instructions.

---

## API Quick Reference

### Initialization
```cpp
int8_t begin(bool enableEPR = true, bool enablePPS = true);
int8_t waitForStartup(uint32_t timeoutMs = 2000);
int8_t waitForPDOs(uint32_t timeoutMs = 8000);
void task();  // Call every loop()
```

### Voltage Control
```cpp
uint8_t setVoltage(uint16_t voltage_mV, uint16_t minCurrent_mA = 1000);
uint8_t set5V(uint16_t minCurrent_mA = 1000);
uint8_t set12V(uint16_t minCurrent_mA = 1000);
uint8_t set20V(uint16_t minCurrent_mA = 1000);
uint8_t setPPS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000);
uint8_t setAVS(uint16_t voltage_mV, uint16_t maxCurrent_mA = 3000);
```

### Monitoring
```cpp
uint16_t getVoltage_mV();
uint16_t getCurrent_mA();
uint32_t getPower_mW();
int8_t getTemperature_C();
bool isFault();
String getFaultString();
bool isPDConnected();
```

### Protection
```cpp
bool setOVPOffset_mV(uint16_t offset_mV);
bool setUVPThreshold(uint8_t uvpMode);  // UVP_80PCT, UVP_75PCT, UVP_70PCT
bool setOCPThreshold_mA(uint16_t mA);
bool setOTPThreshold_C(uint8_t degC);
```

See **API_REFERENCE.md** for complete documentation.

---

## Important Notes

### ⚠️ PPS/AVS Keep-Alive Requirement

**You MUST call `pd.task()` in your loop every iteration** when using PPS or AVS mode. The USB-PD specification requires periodic RDO refresh (~10 second timeout).

```cpp
void loop() {
    pd.task();  // ← MANDATORY
    // Your code here...
}
```

Without this, the charger will revert to 5V after ~5 seconds.

### ⚡ I2C Clock Speed

Always configure I2C to 400 kHz:
```cpp
Wire.setClock(400000);
```

### 🔌 Power Supply

The AP33772S requires stable 3.3V. Ensure your board's regulator can supply sufficient current.

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "AP33772S not found" | Check I2C address (0x52), verify SDA/SCL wiring, check 3.3V power |
| Negotiation timeout | Try different charger, ensure USB-C cable is inserted |
| PPS reverts to 5V | **Call `pd.task()` every loop iteration** |
| Temperature reads 0 | Verify NTC thermistor calibration |

---

## Hardware Compatibility

| Platform | SDA Pin | SCL Pin | Status |
|----------|---------|---------|--------|
| **RP2040** | GPIO 20 | GPIO 21 | ✅ Tested |
| **ESP32** | GPIO 21 | GPIO 22 | ✅ Tested |
| **Arduino Uno** | A4 | A5 | ⚠️ Limited resources |
| **Arduino Mega** | SDA | SCL | ✅ Compatible |

---

## License

MIT License — See LICENSE file for details.

---

## References

- **Datasheet**: [AP33772S DS46176 Rev. 9-2](https://www.diodes.com/)
- **USB PD Spec**: [USB Power Delivery 3.1](https://www.usb.org/)
- **Repository**: [github.com/Rau7han/AP33772S-USBPD-Library](https://github.com/Rau7han/AP33772S-USBPD-Library)

---

**Maintained by:** [@Rau7han](https://github.com/Rau7han)  
**Last Updated:** 2026-05-03
