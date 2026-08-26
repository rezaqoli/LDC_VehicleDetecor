#ifndef DASHBOARD_HTML_H
#define DASHBOARD_HTML_H

const char* DASHBOARD_HTML = R"RAW(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>LDC1614 Fleet Dashboard v5.2</title>
<style>
:root { color-scheme: light dark; }
body { font-family: Arial, sans-serif; margin: 16px; background: #f4f6f8; color: #222; }
.card { background: #fff; padding: 16px; margin-bottom: 14px; border-radius: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }
h2 { margin-top: 0; color: #234; font-size: 18px; }
.row { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin-top: 8px; }
label { font-weight: 600; font-size: 13px; }
input, select { padding: 6px; min-width: 70px; font-size: 13px; }
button { padding: 8px 14px; cursor: pointer; background: #0d6efd; color: #fff; border: none; border-radius: 6px; margin: 4px 4px 0 0; font-size: 13px; }
button:hover { background: #0b5ed7; }
button.danger { background: #dc3545; }
button.danger:hover { background: #b02a37; }
table { width: 100%; border-collapse: collapse; margin-top: 8px; font-size: 13px; }
th, td { border: 1px solid #d9dde2; padding: 7px; text-align: center; }
th { background: #eef3f8; }
.status-connected { color: #198754; font-weight: bold; }
.status-disconnected { color: #dc3545; font-weight: bold; }
.event-log { height: 260px; overflow-y: auto; background: #13212f; color: #8ff4a2; padding: 10px; font-family: Consolas, monospace; font-size: 12px; border-radius: 8px; }
.small { font-size: 12px; color: #555; }
.grid-9 { display: grid; grid-template-columns: repeat(9, 1fr); gap: 6px; }
.grid-5 { display: grid; grid-template-columns: repeat(5, 1fr); gap: 6px; }
.grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }
.grid-2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px; }
.class-result { background:#d4edda; border:1px solid #28a745; padding:10px; border-radius:6px; margin-top:8px; font-size:14px; }
</style>
</head>
<body>
<h1>🚗 LDC1614 Fleet Dashboard v5.2</h1>
<p>Connection: <span id="connStatus" class="status-disconnected">⚪ Disconnected</span></p>

<div class="card">
  <h2>⚙️ System Commands</h2>
  <button onclick="sendCmd('CALIBRATE')">Calibrate</button>
  <button onclick="sendCmd('GET_CONFIG')">Get Config</button>
  <button onclick="sendCmd('GET_SPEED_CONFIG')">Get Speed Config</button>
  <button onclick="sendCmd('GET_SPEED_STATE')">Get Speed State</button>
  <button onclick="sendCmd('GET_STATUS')">Get Status</button>
  <button onclick="sendCmd('GET_NOISE')">Get Noise</button>
  <button onclick="sendCmd('GET_CPU')">Get CPU</button>
  <button onclick="refreshAllData()">Refresh All</button>
  <button class="danger" onclick="if(confirm('Reboot ESP?')) sendCmd('RESET')">🔄 RESET</button>
</div>

<div class="card">
  <h2>📊 Live Sensor Readout</h2>
  <table id="sensorTable">
    <tr><th>Sensor</th><th>Ch</th><th>Raw</th><th>Baseline</th><th>Std</th><th>RMS</th><th>Noise %</th><th>Status</th><th>Action</th></tr>
  </table>
</div>

<div class="card">
  <h2>🧠 Detector Controls</h2>
  <div class="row">
    <label>Confirm samples:</label><input type="number" id="det_confirm" value="3" step="1">
    <label>Min samples:</label><input type="number" id="det_min_samples" value="6" step="1">
    <label>Peak ratio:</label><input type="number" id="det_peak_ratio" value="1.8" step="0.1">
    <label>Enter hyst:</label><input type="number" id="det_enter_hyst" value="0.70" step="0.01">
    <label>Exit hyst:</label><input type="number" id="det_exit_hyst" value="0.55" step="0.01">
    <button onclick="sendDetectorConfig()">Apply Detector</button>
  </div>
</div>

<div class="card">
  <h2>🔧 LC Configuration</h2>
  <div class="row">
    <label>Sensor:</label><select id="lc_s"><option value="0">0</option><option value="1">1</option></select>
    <label>Ch:</label><select id="lc_ch"><option>0</option><option>1</option><option>2</option><option>3</option></select>
    <label>L:</label><input type="number" id="lc_l" value="13" step="0.1">
    <label>C:</label><input type="number" id="lc_c" value="100" step="1">
    <button onclick="sendLC()">Apply SET_LC</button>
  </div>
</div>

<div class="card">
  <h2>📏 Thresholds</h2>
  <div class="row">
    <label>Param:</label><select id="th_param"><option>enter</option><option>exit_ratio</option><option>hysteresis</option></select>
    <label>Value:</label><input type="number" id="th_val" value="0.0008" step="0.0001">
    <button onclick="sendThreshold()">Apply SET_THRESHOLD</button>
  </div>
</div>

<div class="card">
  <h2>🛣️ Speed Pair Setups</h2>
  <table>
    <tr><th>Pair</th><th>Enable</th><th>Distance</th><th>A Sens</th><th>A Ch</th><th>B Sens</th><th>B Ch</th><th>Last Speed</th><th>Action</th></tr>
    <tr>
      <td>0</td><td><select id="sp0_en"><option value="1">ON</option><option value="0">OFF</option></select></td>
      <td><input type="number" id="sp0_dist" value="0.4" step="0.1"></td>
      <td><select id="sp0_s1"><option value="0">S1</option><option value="1">S2</option></select></td>
      <td><select id="sp0_c1"><option>0</option><option>1</option><option>2</option><option>3</option></select></td>
      <td><select id="sp0_s2"><option value="0">S1</option><option value="1" selected>S2</option></select></td>
      <td><select id="sp0_c2"><option>0</option><option>1</option><option>2</option><option>3</option></select></td>
      <td id="sp0_speed">-</td><td><button onclick="sendSpeedPair(0)">Apply</button></td>
    </tr>
    <tr>
      <td>1</td><td><select id="sp1_en"><option value="1">ON</option><option value="0">OFF</option></select></td>
      <td><input type="number" id="sp1_dist" value="0.4" step="0.1"></td>
      <td><select id="sp1_s1"><option value="0">S1</option><option value="1">S2</option></select></td>
      <td><select id="sp1_c1"><option>0</option><option selected>1</option><option>2</option><option>3</option></select></td>
      <td><select id="sp1_s2"><option value="0">S1</option><option value="1" selected>S2</option></select></td>
      <td><select id="sp1_c2"><option>0</option><option selected>1</option><option>2</option><option>3</option></select></td>
      <td id="sp1_speed">-</td><td><button onclick="sendSpeedPair(1)">Apply</button></td>
    </tr>
    <tr>
      <td>2</td><td><select id="sp2_en"><option value="1">ON</option><option value="0">OFF</option></select></td>
      <td><input type="number" id="sp2_dist" value="0.4" step="0.1"></td>
      <td><select id="sp2_s1"><option value="0">S1</option><option value="1">S2</option></select></td>
      <td><select id="sp2_c1"><option>0</option><option>1</option><option selected>2</option><option>3</option></select></td>
      <td><select id="sp2_s2"><option value="0">S1</option><option value="1" selected>S2</option></select></td>
      <td><select id="sp2_c2"><option>0</option><option>1</option><option selected>2</option><option>3</option></select></td>
      <td id="sp2_speed">-</td><td><button onclick="sendSpeedPair(2)">Apply</button></td>
    </tr>
    <tr>
      <td>3</td><td><select id="sp3_en"><option value="1">ON</option><option value="0">OFF</option></select></td>
      <td><input type="number" id="sp3_dist" value="0.4" step="0.1"></td>
      <td><select id="sp3_s1"><option value="0">S1</option><option value="1">S2</option></select></td>
      <td><select id="sp3_c1"><option>0</option><option>1</option><option>2</option><option selected>3</option></select></td>
      <td><select id="sp3_s2"><option value="0">S1</option><option value="1" selected>S2</option></select></td>
      <td><select id="sp3_c2"><option>0</option><option>1</option><option>2</option><option selected>3</option></select></td>
      <td id="sp3_speed">-</td><td><button onclick="sendSpeedPair(3)">Apply</button></td>
    </tr>
  </table>
</div>

<div class="card">
  <h2>⚖️ Auto Threshold</h2>
  <div class="row">
    <label>Auto:</label><select id="auto_enable"><option value="1">ON</option><option value="0">OFF</option></select>
    <label>Enter sigma:</label><input type="number" id="auto_enter_sigma" value="6.0" step="0.1">
    <label>Abs sigma:</label><input type="number" id="auto_abs_sigma" value="3.0" step="0.1">
    <button onclick="sendAutoThreshold()">Apply Auto Threshold</button>
  </div>
</div>

<div class="card">
  <h2>⚖️ Speed Default KMH</h2>
  <div class="row">
    <label>Default KMH:</label><input type="number" id="default_kmh" value="90.0" step="0.1">
    <button onclick="sendSpeedKMH()">Apply Default KMH</button>
  </div>
</div>

<div class="card">
  <h2>⏱️ Event Range</h2>
  <div class="row">
    <label>Min (ms):</label><input type="number" id="er_min" value="20">
    <label>Max (ms):</label><input type="number" id="er_max" value="5000">
    <button onclick="sendEventRange()">Apply SET_EVENT_RANGE</button>
  </div>
</div>

<div class="card">
  <h2>🚗 Vehicle Classification — Length Bounds (meters)</h2>
  <div class="grid-9">
    <div><label class="small">Motor &lt;</label><input type="number" id="cl_motor" value="2.4" step="0.1"></div>
    <div><label class="small">Car &lt;</label><input type="number" id="cl_car" value="4.0" step="0.1"></div>
    <div><label class="small">Pickup &lt;</label><input type="number" id="cl_pickup" value="5.0" step="0.1"></div>
    <div><label class="small">Van &lt;</label><input type="number" id="cl_van" value="6.5" step="0.1"></div>
    <div><label class="small">Bus &lt;</label><input type="number" id="cl_bus" value="11.0" step="0.1"></div>
    <div><label class="small">TruckS &lt;</label><input type="number" id="cl_truckS" value="8.0" step="0.1"></div>
    <div><label class="small">Truck2 &lt;</label><input type="number" id="cl_truck2" value="10.5" step="0.1"></div>
    <div><label class="small">Truck3 &lt;</label><input type="number" id="cl_truck3" value="14.0" step="0.1"></div>
    <div><label class="small">Truck4+ ≥</label><input type="number" id="cl_truck4" value="14.0" step="0.1"></div>
  </div>

  <h2 style="margin-top:14px; font-size:15px;">🔩 Axle Detection</h2>
  <div class="row">
    <label>Peak Prominence (0-1):</label><input type="number" id="cl_prom" value="0.35" step="0.05">
    <label>Min Axle Dist (ms):</label><input type="number" id="cl_axle_ms" value="80" step="10">
  </div>

  <h2 style="margin-top:14px; font-size:15px;">⏱️ Shape Timing (ms)</h2>
  <div class="row">
    <label>Rise Short:</label><input type="number" id="cl_rise_short" value="45" step="1">
    <label>Rise Mid:</label><input type="number" id="cl_rise_mid" value="160" step="1">
    <label>Rise Long:</label><input type="number" id="cl_rise_long" value="300" step="1">
  </div>

  <h2 style="margin-top:14px; font-size:15px;">⚡ Energy & Crest</h2>
  <div class="grid-5">
    <div><label class="small">Energy Low</label><input type="number" id="cl_energy_low" value="0.00001" step="0.00001"></div>
    <div><label class="small">Energy Mid</label><input type="number" id="cl_energy_mid" value="0.00008" step="0.00001"></div>
    <div><label class="small">Energy High</label><input type="number" id="cl_energy_high" value="0.00020" step="0.00001"></div>
    <div><label class="small">Crest Spiky</label><input type="number" id="cl_crest_spiky" value="1.6" step="0.1"></div>
    <div><label class="small">Crest Broad</label><input type="number" id="cl_crest_broad" value="1.2" step="0.1"></div>
  </div>

  <h2 style="margin-top:14px; font-size:15px;">🎯 Symmetry & Width</h2>
  <div class="grid-4">
    <div><label class="small">Skew Tol</label><input type="number" id="cl_skew_tol" value="0.35" step="0.01"></div>
    <div><label class="small">Skew High</label><input type="number" id="cl_skew_high" value="0.75" step="0.01"></div>
    <div><label class="small">COM Min</label><input type="number" id="cl_com_min" value="0.33" step="0.01"></div>
    <div><label class="small">COM Max</label><input type="number" id="cl_com_max" value="0.50" step="0.01"></div>
  </div>
  <div class="grid-4" style="margin-top:6px;">
    <div><label class="small">Width Mid</label><input type="number" id="cl_width_mid" value="0.20" step="0.01"></div>
    <div><label class="small">Width Wide</label><input type="number" id="cl_width_wide" value="0.40" step="0.01"></div>
    <div><label class="small">Std High</label><input type="number" id="cl_std_high" value="0.35" step="0.01"></div>
    <div><label class="small">Std Low</label><input type="number" id="cl_std_low" value="0.18" step="0.01"></div>
  </div>

  <div class="row" style="margin-top:12px;">
    <button onclick="sendClassify()">💾 Apply SET_CLASSIFY</button>
  </div>
</div>

<div class="card" style="border-left: 4px solid #28a745;">
  <h2>🛣️ Dual Loop & Speed Match</h2>
  <div id="dualStatus" style="padding:8px; margin-bottom:10px; border-radius:6px; background:#fff3cd; border:1px solid #ffc107;">
    ⚪ Status unknown — request config first.
  </div>
  <div class="row">
    <label>Dual Loop:</label><select id="cfg_dual"><option value="0">OFF</option><option value="1">ON</option></select>
    <label>Distance (m):</label><input type="number" id="cfg_dist" value="0.4" step="0.1">
    <label>Loop A Sensor:</label><select id="cfg_s1"><option value="0">Sensor 1</option><option value="1">Sensor 2</option></select>
    <label>Loop A Ch:</label><select id="cfg_ch1"><option>0</option><option>1</option><option>2</option><option>3</option></select>
    <label>Loop B Sensor:</label><select id="cfg_s2"><option value="0">Sensor 1</option><option value="1">Sensor 2</option></select>
    <label>Loop B Ch:</label><select id="cfg_ch2"><option>0</option><option>1</option><option>2</option><option>3</option></select>
    <button onclick="sendCFG()">Apply CONFIG</button>
  </div>
</div>

<div class="card">
  <h2>📊 CPU Usage</h2>
  <div class="small">Core 0: <span id="cpu0">0</span>% • Core 1: <span id="cpu1">0</span>%</div>
</div>

<div class="card">
  <h2>📨 Event Log & Classification Results</h2>
  <div id="eventLog" class="event-log">[System] Ready</div>
</div>

<script>
// ------------------------------------------------------------
//  Dashboard JavaScript – complete implementation
// ------------------------------------------------------------

let ws;
const IP = window.location.hostname;

// Data storage
let rawData = { 0: [0,0,0,0], 1: [0,0,0,0] };
let baselineData = { 0: [0,0,0,0], 1: [0,0,0,0] };
let noiseData = { 0: {std:[0,0,0,0], rms:[0,0,0,0], pct:[0,0,0,0] }, 1: {std:[0,0,0,0], rms:[0,0,0,0], pct:[0,0,0,0] } };

function el(id) { return document.getElementById(id); }

function connect() {
  ws = new WebSocket('ws://' + IP + ':81/');
  ws.onopen = () => {
    el('connStatus').innerText = '🟢 Connected';
    el('connStatus').className = 'status-connected';
    log('[System] Connected to WebSocket');
    refreshAllData();
    // start periodic updates
    // if (window._refreshInterval) clearInterval(window._refreshInterval);
    // window._refreshInterval = setInterval(() => {
    //   sendCmd('GET_STATUS');
    //   sendCmd('GET_NOISE');
    //   sendCmd('GET_SPEED_STATE');
    //   sendCmd('GET_CPU');
    // }, 2000);
  };
  ws.onclose = () => {
    el('connStatus').innerText = '🔴 Disconnected';
    el('connStatus').className = 'status-disconnected';
    log('[System] Disconnected. Reconnecting in 3s...');
    if (window._refreshInterval) clearInterval(window._refreshInterval);
    setTimeout(connect, 3000);
  };
  ws.onerror = () => log('[System] WebSocket Error');
  ws.onmessage = (e) => handleMessage(e.data);
}

function log(msg) {
  const logEl = el('eventLog');
  const time = new Date().toLocaleTimeString();
  logEl.innerHTML = '[' + time + '] ' + msg + '<br>' + logEl.innerHTML;
  if (logEl.children.length > 200) logEl.lastElementChild?.remove();
}

function sendCmd(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(cmd);
    log('[Sent] ' + cmd);
  }
}

// -------------------- Senders --------------------
function sendLC() {
  sendCmd('SET_LC|' + el('lc_s').value + '|' + el('lc_ch').value + '|' + el('lc_l').value + '|' + el('lc_c').value);
}
function sendChannelCalibration(sensor, ch) {
  sendCmd('CALIBRATE_CHANNEL|' + sensor + '|' + ch);
}
function sendThreshold() {
  sendCmd('SET_THRESHOLD|' + el('th_param').value + '|' + el('th_val').value);
}
function sendAutoThreshold() {
  sendCmd('SET_AUTO_THRESH|' + el('auto_enable').value + '|' + el('auto_enter_sigma').value + '|' + el('auto_abs_sigma').value);
}
function sendSpeedKMH() {
  sendCmd('SET_DEFAULT_KMH|' + el('default_kmh').value);
}
function sendEventRange() {
  sendCmd('SET_EVENT_RANGE|' + el('er_min').value + '|' + el('er_max').value);
}
function sendClassify() {
  const vals = [
    el('cl_motor').value, el('cl_car').value, el('cl_pickup').value,
    el('cl_van').value, el('cl_bus').value, el('cl_truckS').value,
    el('cl_truck2').value, el('cl_truck3').value, el('cl_truck4').value,
    el('cl_prom').value, el('cl_axle_ms').value,
    el('cl_rise_short').value, el('cl_rise_mid').value, el('cl_rise_long').value,
    el('cl_energy_low').value, el('cl_energy_mid').value, el('cl_energy_high').value,
    el('cl_crest_spiky').value, el('cl_crest_broad').value,
    el('cl_skew_tol').value, el('cl_skew_high').value,
    el('cl_com_min').value, el('cl_com_max').value,
    el('cl_width_mid').value, el('cl_width_wide').value,
    el('cl_std_high').value, el('cl_std_low').value
  ];
  sendCmd('SET_CLASSIFY|' + vals.join('|'));
}
function sendCFG() {
  sendCmd('CONFIG|' + el('cfg_dual').value + '|' + el('cfg_dist').value + '|' + el('cfg_s1').value + '|' + el('cfg_ch1').value + '|' + el('cfg_s2').value + '|' + el('cfg_ch2').value);
}
function sendSpeedPair(idx) {
  sendCmd('CONFIG_SET|' + idx + '|' + el('sp' + idx + '_en').value + '|' + el('sp' + idx + '_dist').value + '|' + el('sp' + idx + '_s1').value + '|' + el('sp' + idx + '_c1').value + '|' + el('sp' + idx + '_s2').value + '|' + el('sp' + idx + '_c2').value);
}
function sendDetectorConfig() {
  sendCmd('SET_DETECTOR|' + el('det_confirm').value + '|' + el('det_min_samples').value + '|' + el('det_peak_ratio').value + '|' + el('det_enter_hyst').value + '|' + el('det_exit_hyst').value);
}

function refreshAllData() {
  sendCmd('GET_STATUS');
  sendCmd('GET_NOISE');
  sendCmd('GET_SPEED_CONFIG');
  sendCmd('GET_SPEED_STATE');
  sendCmd('GET_CPU');
  sendCmd('GET_CONFIG');
}

// -------------------- Message parser --------------------
function handleMessage(data) {
  if (data.startsWith('SENSOR_DATA|')) {
    parseSensorData(data);
  } else if (data.startsWith('EVENT|')) {
    parseEvent(data);
  } else if (data.startsWith('SPEED|') || data.startsWith('SPEED_STATE|')) {
    parseSpeedMessage(data);
  } else if (data.startsWith('CONFIG|')) {
    parseConfig(data);
  } else if (data.startsWith('STATUS')) {
    parseStatus(data);
  } else if (data.startsWith('CONFIG_ACK|')) {
    parseConfigAck(data);
  } else if (data.startsWith('CPU_ACK|')) {
    const parts = data.split('|');
    el('cpu0').innerText = parts[1] || '0';
    el('cpu1').innerText = parts[2] || '0';
  } else if (data.startsWith('NOISE')) {
    parseNoise(data);
  } else if (data.startsWith('CALIBRATION_STARTED')) {
    log('[Ack] ' + data);
  } else if (data.includes('_ACK')) {
    log('[Ack] ' + data);
  } else {
    log('[Msg] ' + data);
  }
}

// -------- SENSOR_DATA --------
function parseSensorData(data) {
  // format: SENSOR_DATA|val1,val2,...,val8|
  const parts = data.split('|');
  if (parts.length < 3) return;
  const vals = parts[1].split(',').map(Number);
  if (vals.length !== 8) return;
  for (let s=0; s<2; s++) {
    for (let ch=0; ch<4; ch++) {
      rawData[s][ch] = vals[s*4 + ch];
    }
  }
  updateSensorTable();
}

// -------- STATUS --------
function parseStatus(data) {
  // STATUS|S1C0:1234|S1C1:... etc.
  const parts = data.split('|');
  for (let i=1; i<parts.length; i++) {
    const pair = parts[i].split(':');
    if (pair.length !== 2) continue;
    const id = pair[0]; // e.g. S1C0
    const val = parseFloat(pair[1]);
    const s = id[1] === '1' ? 0 : 1; // 'S1' -> 0, 'S2' -> 1
    const ch = parseInt(id[3]); // C0..C3
    if (!isNaN(val) && s<2 && ch<4) {
      baselineData[s][ch] = val;
    }
  }
  updateSensorTable();
}

// -------- NOISE --------
function parseNoise(data) {
  // NOISE|S1C0:std/rms/pct|...
  const parts = data.split('|');
  for (let i=1; i<parts.length; i++) {
    const pair = parts[i].split(':');
    if (pair.length !== 2) continue;
    const id = pair[0];
    const vals = pair[1].split('/').map(Number);
    if (vals.length !== 3) continue;
    const s = id[1] === '1' ? 0 : 1;
    const ch = parseInt(id[3]);
    if (s<2 && ch<4) {
      noiseData[s].std[ch] = vals[0];
      noiseData[s].rms[ch] = vals[1];
      noiseData[s].pct[ch] = vals[2];
    }
  }
  updateSensorTable();
}

// -------- update sensor table --------
function updateSensorTable() {
  const table = el('sensorTable');
  if (table.rows.length !== 9) {
    while (table.rows.length > 1) table.deleteRow(1);
    for (let s=0; s<2; s++) {
      for (let ch=0; ch<4; ch++) {
        const row = table.insertRow();
        row.innerHTML = `<td>S${s + 1}</td><td>C${ch}</td><td>-</td><td>-</td><td>-</td><td>-</td><td>-</td><td>---</td><td><button type="button" onclick="sendChannelCalibration(${s}, ${ch})">Calibrate</button></td>`;
      }
    }
  }

  for (let s=0; s<2; s++) {
    for (let ch=0; ch<4; ch++) {
      const row = table.rows[1 + s * 4 + ch];
      if (!row) continue;
      const raw = rawData[s][ch] || 0;
      const baseline = baselineData[s][ch] || 0;
      const std = noiseData[s].std[ch] || 0;
      const rms = noiseData[s].rms[ch] || 0;
      const pct = noiseData[s].pct[ch] || 0;
      // status: we don't have explicit status; we can show "OK" if raw not zero, else "---"
      const status = (raw !== 0) ? 'OK' : '---';
      row.cells[2].innerText = raw;
      row.cells[3].innerText = baseline.toFixed(1);
      row.cells[4].innerText = std.toFixed(2);
      row.cells[5].innerText = rms.toFixed(2);
      row.cells[6].innerText = pct.toFixed(2) + '%';
      row.cells[7].innerText = status;
    }
  }
}

// -------- EVENT --------
function parseEvent(data) {
  // EVENT|channel|start|end|duration|class|length?
  // from reportEvent format: we need to deduce. In main.cpp reportEvent sends: "EVENT|%s|%lu|%lu|%.0f|%s|%.2f"
  // Actually reportEvent in VehicleDetector.cpp sends: "EVENT|%s|%lu|%lu|%.0f|%s|%.2f"
  const parts = data.split('|');
  if (parts.length < 7) return;
  const channel = parts[1];
  const start = parts[2];
  const end = parts[3];
  const dur = parts[4];
  const cls = parts[5];
  const len = parts[6];
  // log(`🚗 EVENT ${channel}  class:${cls}  len:${len}m  dur:${dur}ms  [${start}→${end}]`);
  log(data);
}

// -------- SPEED messages --------
function parseSpeedMessage(data) {
  // SPEED|idx:0|speed:45.2|len:4.8|type:Car|delay:12.34|dist:0.40|a:S1C0|b:S2C0
  // or SPEED_STATE|idx:0|valid:1|speed:...
  const parts = data.split('|');
  const values = {};
  for (let i = 1; i < parts.length; i++) {
    const kv = parts[i].split(':');
    if (kv.length === 2) values[kv[0]] = kv[1];
  }
  if (values.idx === undefined) return;
  const idx = parseInt(values.idx);
  if (isNaN(idx) || idx < 0 || idx > 3) return;

  const speedCell = el('sp' + idx + '_speed');
  if (speedCell) {
    if (data.startsWith('SPEED_STATE|')) {
      if (values.valid === '1') {
        speedCell.innerText = (values.speed || '0') + ' km/h';
      } else {
        speedCell.innerText = '-';
      }
    } else {
      speedCell.innerText = (values.speed || '0') + ' km/h - ' + (values.len || '0') + 'm';
      log(`🏁 SPEED pair ${idx}: ${values.speed} km/h, ${values.type}, len=${values.len}m`);
    }
  }

  if (idx === 0) {
    const statusDiv = el('dualStatus');
    if (values.dual !== undefined) {
      const dualOn = values.dual === '1';
      statusDiv.innerHTML = `🟢 Pair 0 ${dualOn ? 'ON' : 'OFF'} | Speed: ${values.speed || '--'} km/h | Type: ${values.type || '--'} | Delay: ${values.delay || '--'} ms`;
    }
  }
}

// -------- CONFIG_ACK --------
function parseConfigAck(data) {
  const values = {};
  data.substring(11).split('|').forEach(part => {
    const kv = part.split(':');
    if (kv.length === 2) values[kv[0]] = kv[1];
  });
  const idx = values.idx !== undefined ? parseInt(values.idx) : 0;
  if (!isNaN(idx) && idx >= 0 && idx < 4) {
    // update the corresponding speed pair row
    if (values.dual !== undefined) el('sp' + idx + '_en').value = values.dual;
    if (values.dist !== undefined) el('sp' + idx + '_dist').value = values.dist;
    if (values.s1 !== undefined) el('sp' + idx + '_s1').value = values.s1;
    if (values.c1 !== undefined) el('sp' + idx + '_c1').value = values.c1;
    if (values.s2 !== undefined) el('sp' + idx + '_s2').value = values.s2;
    if (values.c2 !== undefined) el('sp' + idx + '_c2').value = values.c2;
  }
  // if idx not given, assume it's for pair 0 (legacy)
  if (values.idx === undefined || values.idx === '0') {
    if (values.dual !== undefined) el('cfg_dual').value = values.dual;
    if (values.dist !== undefined) el('cfg_dist').value = values.dist;
    if (values.s1 !== undefined) el('cfg_s1').value = values.s1;
    if (values.c1 !== undefined) el('cfg_ch1').value = values.c1;
    if (values.s2 !== undefined) el('cfg_s2').value = values.s2;
    if (values.c2 !== undefined) el('cfg_ch2').value = values.c2;
  }
  log('✅ CONFIG applied: ' + data);
}

// -------- CONFIG (full) --------
function parseConfig(data) {
  // This is a long string with many parameters; we could parse and update the UI but that's complex.
  // For simplicity, just log it and optionally update the speed config section.
  const parts = data.split('|');
  if (parts.length > 2) {
    // try to extract dual, dist, etc.
    const values = {};
    for (let i=1; i<parts.length; i++) {
      const kv = parts[i].split(':');
      if (kv.length === 2) values[kv[0]] = kv[1];
    }
    if (values.dual !== undefined) el('cfg_dual').value = values.dual;
    if (values.dist !== undefined) el('cfg_dist').value = values.dist;
    if (values.s1 !== undefined) el('cfg_s1').value = values.s1;
    if (values.c1 !== undefined) el('cfg_ch1').value = values.c1;
    if (values.s2 !== undefined) el('cfg_s2').value = values.s2;
    if (values.c2 !== undefined) el('cfg_ch2').value = values.c2;
    // also populate speed pair rows?
    // but we can just log
  }
  log('[Config] Received (see console)');
  console.log(data);
}

// initialize connection
connect();
</script>
</body>
</html>
)RAW";

#endif // DASHBOARD_HTML_H
