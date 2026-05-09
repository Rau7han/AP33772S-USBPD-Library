# Spark Analyzer User Guide

## What is Spark Analyzer?

Spark Analyzer is an ESP32-powered Wi-Fi and BLE enabled USB-C Power Delivery (PD) analyzer and Programmable Power Supply (PPS). It provides:

- **Inline USB-C integration** with existing setups
- **Real-time voltage and current monitoring** via Wi-Fi web dashboard or BLE
- **Programmable power control** from 3.3 V to 21 V DC
- **USB-C PD negotiation** for 5V, 9V, 12V, 15V, 20V fixed outputs
- **PPS mode**: 3.3V–21V in 100 mV steps with configurable current limit up to 5A

---

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| Microcontroller | ESP32 (any variant with Wi-Fi + BLE) |
| USB-C PD Controller | Diodes AP33772S |
| Current Sensor | AP33772S internal ADC or INA219 (optional) |
| USB-C Cable | USB 3.1 Gen 2 rated, E-marked for >5A |
| Power Source | USB-C PD charger (5–100W recommended) |

### Wiring Diagram

```
USB-C Charger ──── AP33772S ──── ESP32
                      │
                   SDA → GPIO 21
                   SCL → GPIO 22
                   INT → GPIO 19  (optional)
                   GND → GND
                   VCC → 3.3V
```

For the optional INA219 (higher accuracy current sensing):
```
AP33772S VOUT ── [10mΩ shunt] ── Load
                      │
                  INA219 (I2C address 0x40)
                  SDA → GPIO 21 (shared)
                  SCL → GPIO 22 (shared)
```

---

## First-Time Setup

### 1. Flash the Firmware

**Arduino IDE (example sketch)**:
1. Install the AP33772S Arduino library
2. Select **ESP32 Dev Module** as your board
3. Open **File → Examples → AP33772S → 06_SparkAnalyzer**
4. Click **Upload**

**ESP-IDF (full firmware)**:
```bash
cd SparkAnalyzer/firmware
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

### 2. Connect to Wi-Fi

On first boot, the device starts in **AP mode**:

1. Connect your phone or computer to Wi-Fi network **"SparkAnalyzer"** (password: `spark1234`)
2. Open a browser and go to **http://192.168.4.1/**
3. In the **Wi-Fi Configuration** section, enter your home/office Wi-Fi credentials
4. Click **Save & Reconnect**
5. The device connects to your network and shows its IP address in the Serial Monitor

After successful connection, the dashboard is available at:
- **http://spark.local/** (using mDNS, works on most platforms)
- **http://\<device-ip\>/** (check your router for the IP)

### 3. Connect USB-C Power

Plug a USB-C PD charger into the AP33772S USB-C input. The device will:
1. Detect the charger
2. Read all available PDOs (Power Data Objects)
3. Auto-negotiate the highest available voltage (20V → 12V → 5V fallback)
4. Display measurements on the web dashboard

---

## Web Dashboard

Open **http://spark.local/** in any browser to access the live dashboard.

### Features:
- **Live measurements** — voltage, current, power, temperature (updates every 500ms)
- **Connection status** — PD/Legacy source, CC orientation, thermal de-rating
- **Voltage control** — slider from 3.3V to 21V, PPS and AVS mode buttons
- **Quick presets** — 5V, 9V, 12V, 15V, 20V one-click buttons
- **Output switch** — toggle VOUT on/off
- **PDO table** — all charger capabilities
- **Event log** — real-time event messages
- **Wi-Fi configuration** — update network credentials without reflashing

---

## Power Modes

### Fixed PDO Mode
Standard USB PD fixed voltages negotiated with the charger:
- 5V, 9V, 12V, 15V, 20V (charger-dependent)
- Automatic selection of highest available voltage by default

### PPS Mode (Programmable Power Supply)
Requires a USB-C PD 3.0 PPS-capable charger:
- **Voltage range**: 3.3V – 21V
- **Resolution**: 100 mV hardware steps
- **Current limit**: Up to 5A (charger-dependent)
- Automatic keep-alive refresh every 500ms (PD specification requirement)

### AVS Mode (Extended Power Range)
Requires a USB-C PD 3.1 EPR charger:
- **Voltage range**: 15V – 28V
- **Resolution**: 200 mV hardware steps

---

## BLE App Connection

The Spark Analyzer advertises as **"SparkAnalyzer"** over BLE. Use any generic BLE GATT client app (e.g., nRF Connect, LightBlue) to:

1. Scan for BLE devices
2. Connect to **SparkAnalyzer**
3. Navigate to **Power Measurement Service (0x181A)**
4. Subscribe to notifications for live data at 500ms interval

### Writing Commands via BLE

Connect to the **Power Control Service** and write to:
- **Voltage Set** — send 2 bytes little-endian (e.g., `0xE8 0x03` for 1000 mV)
- **Output Control** — send `0x01` for ON, `0x00` for OFF

---

## Protection Features

All protections are configured automatically at startup:

| Protection | Default Setting | Description |
|-----------|-----------------|-------------|
| OVP (Overvoltage) | VREQ + 2000 mV | Output overvoltage protection |
| UVP (Undervoltage) | 80% of VREQ | Output undervoltage protection |
| OCP (Overcurrent) | Auto (110% PDO max) | Output overcurrent protection |
| OTP (Overtemperature) | 100°C | Thermal shutdown |
| Thermal De-rating | 80°C | Automatic power reduction |

When a fault occurs, the device automatically attempts recovery after 3 seconds.

---

## Troubleshooting

| Symptom | Solution |
|---------|----------|
| "SparkAnalyzer" AP not visible | Check ESP32 power, reflash firmware |
| Can't reach http://spark.local/ | Try the IP address directly; mDNS may not work on some Windows networks |
| No PDOs found | Check USB-C cable (must support PD), try different charger |
| Output reverts to 5V | PPS keep-alive issue — reflash firmware |
| Current reads 0 | Verify NTC/INA219 wiring |
| BLE not connecting | Close other apps using BLE, restart phone BT |

---

## LED Status Indicators

| State | LED Behavior |
|-------|-------------|
| Booting | Solid ON |
| AP mode (provisioning) | Slow blink (2s) |
| Wi-Fi connecting | Fast blink (200ms) |
| Wi-Fi connected | Double blink every 3s |
| PD active | Short pulse every 5s |
| Fault | Rapid blink (100ms) |

*(LED GPIO depends on your hardware; configure in sdkconfig)*

---

*For REST API documentation see [API_REFERENCE.md](API_REFERENCE.md)*  
*For firmware internals see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)*
