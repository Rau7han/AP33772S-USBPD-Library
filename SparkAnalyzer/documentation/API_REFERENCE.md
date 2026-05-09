# Spark Analyzer API Reference

**Base URL**: `http://spark.local/api/` (or `http://<device-ip>/api/`)

---

## Authentication

No authentication required for local network access. For production deployments, consider placing behind a reverse proxy with authentication.

---

## Endpoints

### `GET /`
Returns the live web dashboard (HTML).

---

### `GET /api/status`
Returns current measurements and device status.

**Response** `200 application/json`:
```json
{
  "voltage_mv": 12000,
  "current_ma": 1500,
  "power_mw": 18000,
  "temp_c": 35,
  "vreq_mv": 12000,
  "ireq_ma": 2000,
  "pd_connected": true,
  "legacy": false,
  "cc_flip": false,
  "derating": false,
  "fault": false,
  "output_on": true,
  "wifi_ip": "192.168.1.42",
  "wifi_ok": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `voltage_mv` | uint | Measured output voltage (mV) |
| `current_ma` | uint | Measured output current (mA) |
| `power_mw` | uint | Calculated power (mW) |
| `temp_c` | int | AP33772S NTC temperature (°C) |
| `vreq_mv` | uint | Negotiated voltage (mV) |
| `ireq_ma` | uint | Negotiated current limit (mA) |
| `pd_connected` | bool | USB PD source connected |
| `legacy` | bool | Legacy (non-PD) source connected |
| `cc_flip` | bool | Cable connected on CC2 (flipped) |
| `derating` | bool | Thermal de-rating active |
| `fault` | bool | Fault condition active |
| `output_on` | bool | VOUT switch state |

---

### `GET /api/pdos`
Returns all Power Data Objects advertised by the connected charger.

**Response** `200 application/json`:
```json
{
  "pdos": [
    {
      "index": 1,
      "type": 0,
      "min_mv": 0,
      "max_mv": 5000,
      "max_ma": 3000,
      "epr": false
    },
    {
      "index": 2,
      "type": 0,
      "min_mv": 0,
      "max_mv": 9000,
      "max_ma": 3000,
      "epr": false
    },
    {
      "index": 3,
      "type": 1,
      "min_mv": 3300,
      "max_mv": 21000,
      "max_ma": 5000,
      "epr": false
    }
  ]
}
```

| Field | Values | Description |
|-------|--------|-------------|
| `type` | `0` | Fixed PDO (5V/9V/12V/15V/20V) |
| `type` | `1` | PPS — Programmable Power Supply |
| `type` | `2` | AVS — Adjustable Voltage Supply (EPR) |
| `epr` | `true` | Extended Power Range (>20V) |

---

### `POST /api/voltage`
Request a specific voltage. Auto-selects the best available PDO (prefers PPS for precise control, falls back to Fixed PDO).

**Request** `application/json`:
```json
{
  "voltage_mv": 12000,
  "current_ma": 2000
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `voltage_mv` | uint | — | Target voltage in mV |
| `current_ma` | uint | 1000 | Minimum acceptable current (mA) |

**Response** `200 application/json`:
```json
{
  "ok": true,
  "message": "PDO 3 selected for 12000 mV / 2000 mA",
  "pdo_index": 3
}
```

---

### `POST /api/pps`
Request voltage in PPS (Programmable Power Supply) mode. Voltage is rounded to the nearest 100 mV hardware step.

**Request** `application/json`:
```json
{
  "voltage_mv": 9500,
  "current_ma": 3000
}
```

**Voltage range**: 3300 – 21000 mV (100 mV steps)  
**Current range**: 0 – 5000 mA

**Response** `200 application/json`:
```json
{
  "ok": true,
  "message": "PPS PDO 3 at 9500 mV / 3000 mA",
  "pdo_index": 3
}
```

> **Note**: PPS requires periodic RDO refresh (handled automatically by firmware). Charger reverts to 5V if keep-alive stops (USB PD specification requirement).

---

### `POST /api/avs`
Request voltage in AVS (Adjustable Voltage Supply) mode for EPR chargers. Voltage is rounded to the nearest 200 mV hardware step.

**Request** `application/json`:
```json
{
  "voltage_mv": 20000,
  "current_ma": 3000
}
```

**Voltage range**: 15000 – 28000 mV (200 mV steps)  
**Current range**: 0 – 5000 mA

**Response**: Same format as `/api/pps`.

---

### `POST /api/output`
Enable or disable the VOUT output switch.

**Request** `application/json`:
```json
{ "on": true }
```

**Response** `200 application/json`:
```json
{
  "ok": true,
  "on": true,
  "message": "Output ON"
}
```

---

### `POST /api/reset`
Issue a USB PD hard reset. The device will re-enumerate and re-negotiate PDOs.

**Request**: No body required.

**Response** `200 application/json`:
```json
{ "ok": true, "message": "Hard reset issued" }
```

---

### `POST /api/wifi`
Save new Wi-Fi credentials and trigger reconnection.

**Request** `application/json`:
```json
{
  "ssid": "MyNetwork",
  "pass": "mypassword"
}
```

**Response** `200 application/json`:
```json
{ "ok": true, "message": "Credentials saved — reconnecting" }
```

> **Note**: The device will disconnect from the current network and attempt to connect to the new one. If connection fails, it returns to AP mode.

---

## BLE GATT Interface

### Power Measurement Service — UUID `0x181A`

| Characteristic | UUID | Properties | Format | Description |
|----------------|------|------------|--------|-------------|
| Voltage | `0x2B18` | Read, Notify | uint16 LE (mV) | Output voltage |
| Current | `0x2AEE` | Read, Notify | uint16 LE (mA) | Output current |
| Power | `0x2B05` | Read, Notify | uint32 LE (mW) | Output power |
| Temperature | `0x2A6E` | Read, Notify | int8 (°C) | Controller temp |

### Power Control Service — UUID `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

| Characteristic | UUID | Properties | Format | Description |
|----------------|------|------------|--------|-------------|
| Voltage Set | `beb5483e-...` | Write, WriteNR | uint16 LE (mV) | Set output voltage |
| Current Set | `beb5483f-...` | Write, WriteNR | uint16 LE (mA) | Set current limit |
| Output Control | `beb54840-...` | Write, WriteNR | uint8 (0/1) | VOUT on/off |
| Status JSON | `beb54841-...` | Read, Notify | string | JSON status |

**Notification interval**: 500 ms when client is subscribed.

---

## Error Responses

| HTTP Code | Meaning |
|-----------|---------|
| 400 | Bad request (invalid JSON, missing required field) |
| 404 | Endpoint not found |
| 500 | Internal server error |

All error responses follow the format:
```json
{ "ok": false, "message": "Error description" }
```

---

## Quick Start Examples

### curl

```bash
# Get current status
curl http://spark.local/api/status

# Set voltage to 12V @ 2A
curl -X POST http://spark.local/api/voltage \
  -H "Content-Type: application/json" \
  -d '{"voltage_mv": 12000, "current_ma": 2000}'

# Set PPS to 9.5V @ 3A
curl -X POST http://spark.local/api/pps \
  -H "Content-Type: application/json" \
  -d '{"voltage_mv": 9500, "current_ma": 3000}'

# Enable output
curl -X POST http://spark.local/api/output \
  -H "Content-Type: application/json" \
  -d '{"on": true}'

# Get all PDOs
curl http://spark.local/api/pdos
```

### Python

```python
import requests

base = "http://spark.local/api"

# Get measurements
status = requests.get(f"{base}/status").json()
print(f"V={status['voltage_mv']}mV  I={status['current_ma']}mA  "
      f"P={status['power_mw']}mW")

# Set PPS voltage
resp = requests.post(f"{base}/pps",
                     json={"voltage_mv": 5000, "current_ma": 3000})
print(resp.json()["message"])

# Log measurements every 500ms
import time
while True:
    d = requests.get(f"{base}/status").json()
    print(f"{time.time():.2f}  {d['voltage_mv']}mV  {d['current_ma']}mA")
    time.sleep(0.5)
```

### JavaScript (browser / Node.js)

```javascript
const BASE = 'http://spark.local/api';

// Poll measurements every 500ms
setInterval(async () => {
  const { voltage_mv, current_ma, power_mw } =
    await fetch(`${BASE}/status`).then(r => r.json());
  console.log(`V=${voltage_mv}mV  I=${current_ma}mA  P=${power_mw}mW`);
}, 500);

// Set voltage
const setVoltage = async (mv, ma = 1000) => {
  const r = await fetch(`${BASE}/voltage`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ voltage_mv: mv, current_ma: ma }),
  });
  return r.json();
};

await setVoltage(12000, 2000);
```

---

*See [USER_GUIDE.md](USER_GUIDE.md) for hardware setup and [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) for firmware architecture.*
