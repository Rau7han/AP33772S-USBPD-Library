# Spark Analyzer Hardware

This directory contains hardware design files for the Spark Analyzer PCB.

## Directory Structure

```
hardware/
├── schematics/     — KiCad schematic files (.sch, .kicad_sch)
├── pcb_layout/     — KiCad PCB layout files (.kicad_pcb)
└── bom.csv         — Bill of Materials
```

## Key Design Points

- **USB-C connectors**: Both input (charger) and output (load) use full-featured 24-pin USB-C receptacles supporting PD 3.1
- **Current shunt**: 10 mΩ / 2W shunt resistor for INA219 current sensing (max 5A continuous)
- **I2C bus**: Shared between AP33772S (0x52) and optional INA219 (0x40)
- **Power path**: AP33772S VOUT → shunt resistor → output USB-C connector
- **3.3V rail**: AMS1117-3.3 fed from 5V USB-C VBUS for ESP32 and AP33772S logic power

## Schematic Notes

1. AP33772S CC1/CC2 pins connect directly to USB-C CC1/CC2 lines
2. AP33772S VOUT drives the programmable buck converter output
3. INT pin has a 10kΩ pull-up to 3.3V; connect to ESP32 GPIO 19
4. ESP32 boot strapping pins (GPIO 0, 2, 15) have appropriate pull resistors

## Manufacturing

Recommended PCB specs:
- 2-layer or 4-layer FR4
- 1oz copper
- HASL or ENIG surface finish
- Min trace: 0.127mm / Min space: 0.127mm
- Board size: 60mm × 30mm (compact inline form factor)
