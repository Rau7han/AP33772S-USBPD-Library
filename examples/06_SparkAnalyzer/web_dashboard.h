/**
 * web_dashboard.h  —  Spark Analyzer Embedded Web Dashboard
 * ─────────────────────────────────────────────────────────────────────────────
 * Self-contained HTML/CSS/JS page served directly from ESP32 flash.
 * Auto-refreshes measurements every 500 ms via fetch() polling.
 * Provides controls for voltage/current adjustment and output switch.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_WEB_DASHBOARD_H
#define SPARK_WEB_DASHBOARD_H

// The full dashboard page stored in program memory to save RAM.
static const char SPARK_DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Spark Analyzer</title>
  <style>
    :root {
      --bg: #0f1117; --card: #1a1d27; --accent: #4f8ef7;
      --green: #2ecc71; --red: #e74c3c; --yellow: #f39c12;
      --text: #e0e0e0; --muted: #888;
      --radius: 12px; --gap: 16px;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', system-ui, sans-serif;
      background: var(--bg); color: var(--text);
      min-height: 100vh; padding: 20px;
    }
    header {
      display: flex; align-items: center; gap: 12px;
      margin-bottom: 24px;
    }
    header svg { width: 36px; height: 36px; }
    header h1 { font-size: 1.6rem; font-weight: 700; }
    header small { color: var(--muted); font-size: 0.85rem; }
    #conn-status {
      margin-left: auto; padding: 4px 12px;
      border-radius: 20px; font-size: 0.8rem;
      background: var(--red); color: #fff;
    }
    #conn-status.ok { background: var(--green); }

    .grid { display: grid; gap: var(--gap); }
    @media (min-width: 600px) {
      .grid-2 { grid-template-columns: 1fr 1fr; }
      .grid-4 { grid-template-columns: repeat(4, 1fr); }
    }

    .card {
      background: var(--card); border-radius: var(--radius);
      padding: 20px; position: relative; overflow: hidden;
    }
    .card h3 { color: var(--muted); font-size: 0.75rem; text-transform: uppercase;
               letter-spacing: 1px; margin-bottom: 8px; }
    .metric { font-size: 2.2rem; font-weight: 700; line-height: 1; }
    .unit   { font-size: 1rem; color: var(--muted); margin-left: 4px; }
    .sub    { font-size: 0.8rem; color: var(--muted); margin-top: 6px; }
    .bar-wrap { background: #2a2d3a; border-radius: 6px; height: 6px; margin-top: 10px; overflow: hidden; }
    .bar { height: 100%; border-radius: 6px; background: var(--accent);
           transition: width 0.4s ease; }

    .badge {
      display: inline-block; padding: 2px 8px;
      border-radius: 10px; font-size: 0.75rem; font-weight: 600;
      background: #2a2d3a;
    }
    .badge.ok     { background: rgba(46,204,113,.2); color: var(--green); }
    .badge.warn   { background: rgba(243,156,18,.2); color: var(--yellow); }
    .badge.error  { background: rgba(231,76,60,.2);  color: var(--red); }

    label { font-size: 0.85rem; color: var(--muted); display: block; margin-bottom: 6px; }
    input[type=range] {
      width: 100%; accent-color: var(--accent); cursor: pointer;
    }
    input[type=number] {
      width: 100%; background: #2a2d3a; border: 1px solid #3a3d4a;
      color: var(--text); padding: 8px 12px; border-radius: 8px;
      font-size: 1rem; outline: none;
    }
    input[type=number]:focus { border-color: var(--accent); }

    .range-labels { display: flex; justify-content: space-between;
                    font-size: 0.75rem; color: var(--muted); margin-top: 4px; }

    button {
      border: none; border-radius: 8px; padding: 10px 18px;
      font-size: 0.9rem; font-weight: 600; cursor: pointer;
      transition: opacity .15s;
    }
    button:hover { opacity: .85; }
    button:active { opacity: .7; }
    .btn-primary { background: var(--accent); color: #fff; }
    .btn-success { background: var(--green); color: #fff; }
    .btn-danger  { background: var(--red);   color: #fff; }
    .btn-row { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 10px; }

    .pdo-table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
    .pdo-table th { color: var(--muted); text-align: left; padding: 6px 8px;
                    border-bottom: 1px solid #2a2d3a; }
    .pdo-table td { padding: 6px 8px; border-bottom: 1px solid #1f2230; }
    .pdo-table tr:last-child td { border: none; }
    .pdo-table tr:hover td { background: rgba(79,142,247,.07); }

    #log {
      background: #0a0c12; border-radius: 8px; padding: 12px;
      height: 160px; overflow-y: auto; font-family: monospace;
      font-size: 0.78rem; color: #8be28b;
    }
    #log p { margin: 2px 0; }
    #log p.err { color: var(--red); }
    #log p.info { color: var(--accent); }

    .section-title {
      font-size: 0.7rem; text-transform: uppercase; letter-spacing: 1px;
      color: var(--muted); margin: 24px 0 12px;
    }

    #output-toggle {
      width: 52px; height: 28px; border-radius: 14px;
      background: var(--red); border: none; cursor: pointer;
      position: relative; transition: background .3s;
    }
    #output-toggle::after {
      content: ''; position: absolute; top: 3px; left: 3px;
      width: 22px; height: 22px; border-radius: 50%;
      background: #fff; transition: left .3s;
    }
    #output-toggle.on { background: var(--green); }
    #output-toggle.on::after { left: 27px; }
  </style>
</head>
<body>

<header>
  <svg viewBox="0 0 36 36" fill="none">
    <circle cx="18" cy="18" r="18" fill="#4f8ef7" opacity=".15"/>
    <path d="M21 6l-8 14h7l-5 10 12-16h-8l6-8z" fill="#4f8ef7"/>
  </svg>
  <div>
    <h1>Spark Analyzer</h1>
    <small>USB-C PD &amp; Programmable Power Supply</small>
  </div>
  <span id="conn-status">Offline</span>
</header>

<!-- Live Metrics -->
<p class="section-title">Live Measurements</p>
<div class="grid grid-4">
  <div class="card">
    <h3>Voltage</h3>
    <div><span class="metric" id="v-val">—</span><span class="unit">mV</span></div>
    <div class="bar-wrap"><div class="bar" id="v-bar" style="width:0%"></div></div>
    <p class="sub" id="v-req">Req: — mV</p>
  </div>
  <div class="card">
    <h3>Current</h3>
    <div><span class="metric" id="i-val">—</span><span class="unit">mA</span></div>
    <div class="bar-wrap"><div class="bar" id="i-bar" style="width:0%;background:var(--green)"></div></div>
    <p class="sub" id="i-req">Req: — mA</p>
  </div>
  <div class="card">
    <h3>Power</h3>
    <div><span class="metric" id="p-val">—</span><span class="unit">mW</span></div>
    <div class="bar-wrap"><div class="bar" id="p-bar" style="width:0%;background:var(--yellow)"></div></div>
    <p class="sub">Instantaneous</p>
  </div>
  <div class="card">
    <h3>Temperature</h3>
    <div><span class="metric" id="t-val">—</span><span class="unit">°C</span></div>
    <div class="bar-wrap"><div class="bar" id="t-bar" style="width:0%;background:var(--red)"></div></div>
    <p class="sub" id="t-status">Normal</p>
  </div>
</div>

<!-- Connection Info -->
<p class="section-title">Connection Status</p>
<div class="grid grid-2">
  <div class="card">
    <h3>Source</h3>
    <p id="src-type" class="badge">—</p>
    <p class="sub" style="margin-top:8px">
      <span id="cc-flip">—</span> &bull; <span id="derate-status">—</span>
    </p>
  </div>
  <div class="card">
    <h3>Fault Status</h3>
    <p id="fault-str" class="badge ok">None</p>
    <div class="btn-row" style="margin-top:12px">
      <button class="btn-primary" onclick="doHardReset()">Hard Reset</button>
      <div style="display:flex;align-items:center;gap:8px">
        <span style="font-size:0.85rem;color:var(--muted)">Output</span>
        <button id="output-toggle" onclick="toggleOutput()" title="Toggle VOUT"></button>
      </div>
    </div>
  </div>
</div>

<!-- Voltage Control -->
<p class="section-title">Voltage Control</p>
<div class="grid grid-2">
  <div class="card">
    <h3>Set Voltage (PPS / Fixed Auto-Select)</h3>
    <label>Target: <span id="volt-label">5000</span> mV</label>
    <input type="range" id="volt-slider" min="3300" max="21000" step="100" value="5000"
           oninput="document.getElementById('volt-label').textContent=this.value">
    <div class="range-labels"><span>3.3 V</span><span>21 V</span></div>
    <label style="margin-top:14px">Min current (mA)</label>
    <input type="number" id="volt-current" value="1000" min="500" max="5000" step="50">
    <div class="btn-row">
      <button class="btn-primary" onclick="setVoltage()">Apply</button>
      <button onclick="setQuick(5000)">5 V</button>
      <button onclick="setQuick(9000)">9 V</button>
      <button onclick="setQuick(12000)">12 V</button>
      <button onclick="setQuick(15000)">15 V</button>
      <button onclick="setQuick(20000)">20 V</button>
    </div>
  </div>
  <div class="card">
    <h3>PPS Mode (Programmable)</h3>
    <label>Voltage: <span id="pps-label">5000</span> mV</label>
    <input type="range" id="pps-slider" min="3300" max="21000" step="100" value="5000"
           oninput="document.getElementById('pps-label').textContent=this.value">
    <div class="range-labels"><span>3.3 V</span><span>21 V</span></div>
    <label style="margin-top:14px">Current limit (mA)</label>
    <input type="number" id="pps-current" value="3000" min="500" max="5000" step="50">
    <div class="btn-row">
      <button class="btn-primary" onclick="setPPS()">Apply PPS</button>
      <button class="btn-success" onclick="setAVS()">Apply AVS</button>
    </div>
  </div>
</div>

<!-- PDO Table -->
<p class="section-title">Power Data Objects (PDOs)</p>
<div class="card">
  <table class="pdo-table">
    <thead>
      <tr><th>#</th><th>Type</th><th>Min V</th><th>Max V</th><th>Max I</th><th>EPR</th></tr>
    </thead>
    <tbody id="pdo-tbody"><tr><td colspan="6" style="color:var(--muted)">Loading...</td></tr></tbody>
  </table>
  <div class="btn-row" style="margin-top:12px">
    <button class="btn-primary" onclick="refreshPDOs()">Refresh PDOs</button>
  </div>
</div>

<!-- Wi-Fi Provisioning -->
<p class="section-title">Wi-Fi Configuration</p>
<div class="card">
  <div class="grid grid-2" style="gap:12px">
    <div>
      <label>SSID</label>
      <input type="text" id="wifi-ssid" placeholder="Network name"
             style="width:100%;background:#2a2d3a;border:1px solid #3a3d4a;color:var(--text);
                    padding:8px 12px;border-radius:8px;font-size:0.95rem;outline:none">
    </div>
    <div>
      <label>Password</label>
      <input type="password" id="wifi-pass" placeholder="Password"
             style="width:100%;background:#2a2d3a;border:1px solid #3a3d4a;color:var(--text);
                    padding:8px 12px;border-radius:8px;font-size:0.95rem;outline:none">
    </div>
  </div>
  <div class="btn-row">
    <button class="btn-primary" onclick="saveWifi()">Save & Reconnect</button>
    <span id="wifi-msg" style="font-size:0.85rem;color:var(--muted)"></span>
  </div>
</div>

<!-- Event Log -->
<p class="section-title">Event Log</p>
<div class="card" style="padding:12px">
  <div id="log"></div>
  <button style="margin-top:8px;background:#2a2d3a;color:var(--muted);" onclick="clearLog()">Clear</button>
</div>

<script>
const MAX_V = 21000, MAX_I = 5000, MAX_P = 100000, MAX_T = 120;
let outputOn = false;

function log(msg, cls='') {
  const d = document.getElementById('log');
  const p = document.createElement('p');
  if (cls) p.className = cls;
  p.textContent = new Date().toLocaleTimeString() + ' — ' + msg;
  d.prepend(p);
  while (d.children.length > 80) d.removeChild(d.lastChild);
}
function clearLog() { document.getElementById('log').innerHTML = ''; }

function setBar(id, val, max) {
  document.getElementById(id).style.width = Math.min(100, (val/max)*100).toFixed(1) + '%';
}

async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    if (!r.ok) throw new Error(r.status);
    const d = await r.json();
    document.getElementById('conn-status').textContent = 'Online';
    document.getElementById('conn-status').className = 'ok';

    document.getElementById('v-val').textContent = d.voltage_mv ?? '—';
    document.getElementById('i-val').textContent = d.current_ma ?? '—';
    document.getElementById('p-val').textContent = d.power_mw  ?? '—';
    document.getElementById('t-val').textContent = d.temp_c    ?? '—';
    document.getElementById('v-req').textContent = 'Req: ' + (d.vreq_mv ?? '—') + ' mV';
    document.getElementById('i-req').textContent = 'Req: ' + (d.ireq_ma ?? '—') + ' mA';

    setBar('v-bar', d.voltage_mv, MAX_V);
    setBar('i-bar', d.current_ma, MAX_I);
    setBar('p-bar', d.power_mw,  MAX_P);
    setBar('t-bar', d.temp_c,    MAX_T);

    const srcEl = document.getElementById('src-type');
    srcEl.textContent = d.pd_connected ? 'USB PD' : (d.legacy ? 'Legacy' : 'None');
    srcEl.className = 'badge ' + (d.pd_connected ? 'ok' : (d.legacy ? 'warn' : 'error'));

    document.getElementById('cc-flip').textContent = d.cc_flip ? 'CC2 (flipped)' : 'CC1';
    document.getElementById('derate-status').textContent = d.derating ? 'Derating active' : 'Normal';

    const faultEl = document.getElementById('fault-str');
    faultEl.textContent = d.fault || 'None';
    faultEl.className = 'badge ' + (d.fault && d.fault !== 'None' ? 'error' : 'ok');

    const tog = document.getElementById('output-toggle');
    outputOn = d.output_on ?? false;
    tog.className = outputOn ? 'on' : '';

    const tStatus = document.getElementById('t-status');
    tStatus.textContent = d.temp_c >= 80 ? 'High — derating' : (d.temp_c >= 60 ? 'Warm' : 'Normal');
  } catch(e) {
    document.getElementById('conn-status').textContent = 'Offline';
    document.getElementById('conn-status').className = '';
  }
}

async function refreshPDOs() {
  try {
    const r = await fetch('/api/pdos');
    const d = await r.json();
    const tbody = document.getElementById('pdo-tbody');
    tbody.innerHTML = '';
    (d.pdos || []).forEach(p => {
      const tr = document.createElement('tr');
      const typeStr = p.type === 0 ? 'Fixed' : (p.type === 1 ? 'PPS' : 'AVS');
      tr.innerHTML = `<td>${p.index}</td><td>${typeStr}</td>
        <td>${p.min_mv} mV</td><td>${p.max_mv} mV</td>
        <td>${p.max_ma} mA</td><td>${p.epr ? '✓' : '—'}</td>`;
      tbody.appendChild(tr);
    });
    log('PDOs refreshed — ' + (d.pdos||[]).length + ' found', 'info');
  } catch(e) { log('PDO fetch error: ' + e, 'err'); }
}

async function setVoltage() {
  const mv = parseInt(document.getElementById('volt-slider').value);
  const ma = parseInt(document.getElementById('volt-current').value);
  try {
    const r = await fetch('/api/voltage', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({voltage_mv: mv, current_ma: ma})
    });
    const d = await r.json();
    log(d.message || ('Voltage set to ' + mv + ' mV'), d.ok ? 'info' : 'err');
  } catch(e) { log('Set voltage error: ' + e, 'err'); }
}

async function setQuick(mv) {
  document.getElementById('volt-slider').value = mv;
  document.getElementById('volt-label').textContent = mv;
  await setVoltage();
}

async function setPPS() {
  const mv = parseInt(document.getElementById('pps-slider').value);
  const ma = parseInt(document.getElementById('pps-current').value);
  try {
    const r = await fetch('/api/pps', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({voltage_mv: mv, current_ma: ma})
    });
    const d = await r.json();
    log(d.message || ('PPS set to ' + mv + ' mV'), d.ok ? 'info' : 'err');
  } catch(e) { log('PPS error: ' + e, 'err'); }
}

async function setAVS() {
  const mv = parseInt(document.getElementById('pps-slider').value);
  const ma = parseInt(document.getElementById('pps-current').value);
  try {
    const r = await fetch('/api/avs', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({voltage_mv: mv, current_ma: ma})
    });
    const d = await r.json();
    log(d.message || ('AVS set to ' + mv + ' mV'), d.ok ? 'info' : 'err');
  } catch(e) { log('AVS error: ' + e, 'err'); }
}

async function toggleOutput() {
  const newState = !outputOn;
  try {
    const r = await fetch('/api/output', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({on: newState})
    });
    const d = await r.json();
    log('Output ' + (newState ? 'ON' : 'OFF'), 'info');
  } catch(e) { log('Output error: ' + e, 'err'); }
}

async function doHardReset() {
  try {
    await fetch('/api/reset', {method: 'POST'});
    log('Hard reset issued', 'info');
  } catch(e) { log('Reset error: ' + e, 'err'); }
}

async function saveWifi() {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pass = document.getElementById('wifi-pass').value;
  if (!ssid) { document.getElementById('wifi-msg').textContent = 'Enter SSID'; return; }
  try {
    await fetch('/api/wifi', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ssid, pass})
    });
    document.getElementById('wifi-msg').textContent = 'Saved — reconnecting...';
    log('Wi-Fi credentials saved for: ' + ssid, 'info');
  } catch(e) { log('Wi-Fi save error: ' + e, 'err'); }
}

// Auto-refresh
fetchStatus();
refreshPDOs();
setInterval(fetchStatus, 500);
</script>
</body>
</html>
)rawliteral";

#endif // SPARK_WEB_DASHBOARD_H
