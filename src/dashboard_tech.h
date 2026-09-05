#ifndef DASHBOARD_TECH_H
#define DASHBOARD_TECH_H

const char* DASHBOARD_TECH_HTML = R"RAW(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>LDC1614 Technician Dashboard</title>
<style>
:root { color-scheme: light dark; }
body { font-family: Arial, sans-serif; margin: 16px; background: #f4f6f8; color: #222; }
.card { background: #fff; padding: 16px; margin-bottom: 14px; border-radius: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }
h1 { margin-top: 0; }
h2 { margin-top: 0; color: #234; font-size: 18px; }
.row { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin-top: 8px; }
label { font-weight: 600; font-size: 13px; }
input, select { padding: 6px; min-width: 70px; font-size: 13px; }
button { padding: 8px 14px; cursor: pointer; background: #0d6efd; color: #fff; border: none; border-radius: 6px; margin: 4px 4px 0 0; font-size: 13px; }
button:hover { background: #0b5ed7; }
button.danger { background: #dc3545; }
button.danger:hover { background: #b02a37; }
button.ok { background: #198754; }
button.ok:hover { background: #146c43; }
table { width: 100%; border-collapse: collapse; margin-top: 8px; font-size: 13px; }
th, td { border: 1px solid #d9dde2; padding: 7px; text-align: center; }
th { background: #eef3f8; }
.status-connected { color: #198754; font-weight: bold; }
.status-disconnected { color: #dc3545; font-weight: bold; }
.small { font-size: 12px; color: #555; }
.event-log { height: 200px; overflow-y: auto; background: #13212f; color: #8ff4a2; padding: 10px; font-family: Consolas, monospace; font-size: 12px; border-radius: 8px; }
.nav a { margin-right: 10px; font-size: 13px; }
</style>
</head>
<body>
<h1>🛠️ LDC1614 Technician Dashboard</h1>
<p class="nav">
  <a href="/dev">/dev (Engineer)</a>
  <a href="/tech">/tech (Technician)</a>
  <a href="/mqtt">/mqtt (Monitor)</a>
</p>
<p>Connection: <span id="connStatus" class="status-disconnected">⚪ Disconnected</span></p>

<div class="card">
  <h2>⚙️ System</h2>
  <div class="row">
    <button onclick="sendCmd('GET_MQTT_CFG')">Get MQTT Cfg</button>
    <button onclick="sendCmd('GET_WIFI')">Get Wi-Fi</button>
    <button onclick="sendCmd('GET_SENSOR_LC')">Get Sensor LC</button>
    <button onclick="sendCmd('GET_LOOP_CFG')">Get Loop Cfg</button>
    <button onclick="sendCmd('GET_TRAFFIC_RULES')">Get Rules</button>
    <button onclick="sendCmd('GET_REPORT_CFG')">Get Report Cfg</button>
    <button onclick="sendCmd('GET_DEFAULT_KMH')">Get Default KMH</button>
    <button onclick="sendCmd('GET_CPU')">Get CPU</button>
    <button onclick="refreshAll()">Refresh All</button>
    <button class="ok" onclick="sendCmd('SAVE_ALL')">💾 Save All to NVS</button>
    <button class="danger" onclick="if(confirm('Reboot ESP?')) sendCmd('RESET')">🔄 RESET</button>
  </div>
</div>

<div class="card">
  <h2>📶 Wi-Fi (read-only)</h2>
  <div class="row">
    <label>SSID:</label><input type="text" id="wf_ssid" value="-" style="min-width:160px" readonly>
    <label>IP:</label><input type="text" id="wf_ip" value="-" style="min-width:120px" readonly>
    <label>RSSI:</label><input type="text" id="wf_rssi" value="-" style="min-width:60px" readonly>
  </div>
</div>

<div class="card">
  <h2>🌐 MQTT Broker Configuration</h2>
  <div class="row">
    <label>Client ID:</label><input type="text" id="mq_id" value="ESP32_Vehicle_Detector" style="min-width:200px">
    <label>Server (host):</label><input type="text" id="mq_server" value="iot.iolink.ir" style="min-width:160px">
    <label>Port:</label><input type="number" id="mq_port" value="1883" step="1">
    <label>IP (optional):</label><input type="text" id="mq_ip" value="0.0.0.0" style="min-width:120px">
  </div>
  <div class="row" style="margin-top:6px;">
    <label>User:</label><input type="text" id="mq_user" value="" style="min-width:120px">
    <label>Pass:</label><input type="password" id="mq_pass" value="" style="min-width:120px">
    <label>LTE APN:</label><input type="text" id="mq_apn" value="shatelmobile" style="min-width:140px">
  </div>
  <div class="row" style="margin-top:6px;">
    <label>Topic Events:</label><input type="text" id="mq_topic_events" value="vehicles/events" style="min-width:180px">
    <label>Topic Commands:</label><input type="text" id="mq_topic_commands" value="vehicles/commands" style="min-width:180px">
    <label>Topic Responses:</label><input type="text" id="mq_topic_responses" value="vehicles/command_responses" style="min-width:200px">
  </div>
  <div class="row" style="margin-top:8px;">
    <button class="ok" onclick="sendMqttCfg()">💾 Apply MQTT (restart required)</button>
  </div>
  <p class="small">Note: changing broker settings requires an ESP restart to take effect.</p>
</div>

<div class="card">
  <h2>🔧 Sensor L &amp; C (per channel)</h2>
  <table>
    <tr><th>Sensor</th><th>Ch</th><th>L (µH)</th><th>C (pF)</th><th>Conv. time</th><th>Driver</th><th>Apply</th></tr>
    <tbody id="lcTbl"></tbody>
  </table>
  <p class="small">Per-channel L/C applied live via SET_LC; saved to NVS automatically.</p>
</div>

<div class="card">
  <h2>🛣️ Loop Configuration (4 pairs)</h2>
  <table>
    <tr><th>Pair</th><th>Enable</th><th>Distance (m)</th><th>A Sens</th><th>A Ch</th><th>B Sens</th><th>B Ch</th><th>Last Speed</th><th>Apply</th></tr>
    <tbody id="lpTbl"></tbody>
  </table>
</div>

<div class="card" style="border-left: 4px solid #dc3545;">
  <h2>🚧 Traffic Rules</h2>
  <div class="row">
    <label>Speed Limit:</label><input type="number" id="rule_speed" value="100" step="1">
    <label>Tolerance:</label><input type="number" id="rule_tol" value="5" step="1">
    <label>Min Distance (m):</label><input type="number" id="rule_dist" value="30" step="1">
    <label>Min Headway (s):</label><input type="number" id="rule_headway" value="1.2" step="0.1">
    <label>Max Headway (ms):</label><input type="number" id="rule_maxheadway" value="20000" step="1000">
  </div>
  <div class="row" style="margin-top:6px;">
    <label>Straddle MS:</label><input type="number" id="rule_straddle_ms" value="100" step="10">
    <label>Straddle Ratio:</label><input type="number" id="rule_straddle_ratio" value="0.25" step="0.05">
    <label>Assume Speed (km/h):</label><input type="number" id="rule_assume" value="0" step="1">
    <button class="ok" onclick="sendRules()">💾 Apply SET_RULES</button>
  </div>
</div>

<div class="card" style="border-left: 4px solid #ffc107;">
  <h2>⏱️ Report Settings</h2>
  <div class="row">
    <label>Interval (min):</label><input type="number" id="rpt_interval" value="20" step="1" min="0">
    <button onclick="sendCmd('SET_REPORT_INTERVAL|' + el('rpt_interval').value)">Set Interval</button>
    <label>Auto Report:</label><select id="rpt_enable"><option value="1">ON</option><option value="0">OFF</option></select>
    <button onclick="sendCmd('SET_REPORT_ENABLE|' + el('rpt_enable').value)">Apply</button>
    <label>Clear on Report:</label><select id="rpt_clear"><option value="0">NO</option><option value="1">YES</option></select>
    <button onclick="sendCmd('SET_REPORT_CLEAR|' + el('rpt_clear').value)">Apply</button>
  </div>
</div>

<div class="card" style="border-left: 4px solid #17a2b8;">
  <h2>🗺️ Loop Geometry</h2>
  <div class="row">
    <label>Site ID:</label><input type="text" id="geo_site" value="SITE-01" style="min-width:80px">
    <label>Lanes:</label><input type="number" id="geo_lanes" value="4" min="1" max="8">
    <label>Lane Width (m):</label><input type="number" id="geo_lane_w" value="3.5" step="0.1">
    <label>Speed Limit:</label><input type="number" id="geo_speed" value="100" step="1">
    <label>Direction (deg):</label><input type="number" id="geo_dir" value="0" step="1">
    <button class="ok" onclick="sendCmd('SET_LOOP_GEOMETRY|' + el('geo_site').value + '|' + el('geo_lanes').value + '|' + el('geo_lane_w').value + '|' + el('geo_speed').value + '|' + el('geo_dir').value)">💾 Set Site</button>
    <button onclick="sendCmd('GET_LOOP_GEOMETRY')">Refresh</button>
  </div>
</div>

<div class="card">
  <h2>📊 CPU</h2>
  <div class="small">Core 0: <span id="cpu0">0</span>% • Core 1: <span id="cpu1">0</span>%</div>
</div>

<div class="card">
  <h2>📨 Activity Log</h2>
  <div id="eventLog" class="event-log">[System] Ready</div>
  <button onclick="el('eventLog').innerHTML='[System] Cleared'">Clear</button>
</div>

<div class="card" style="border-left: 4px solid #20c997;">
  <h2>📡 MQTT Publish Stats</h2>
  <div class="row">
    <span class="kv">Published: <b id="mp_published">0</b></span>
    <span class="kv">Dropped: <b id="mp_dropped">0</b></span>
    <span class="kv">Reconnect: <b id="mp_reconnect">0</b></span>
    <span class="kv">Last err: <b id="mp_err">none</b></span>
  </div>
  <div class="row" style="margin-top:8px;">
    <button onclick="sendCmd('GET_MQTT_PUB_STATS')">Refresh</button>
    <button onclick="sendCmd('RESET_MQTT_PUB_STATS')">Reset Counters</button>
  </div>
</div>

<script>
let ws;
const IP = window.location.hostname;
function el(id){ return document.getElementById(id); }

function connect(){
  ws = new WebSocket('ws://' + IP + ':81/');
  ws.onopen = () => {
    el('connStatus').innerText = '🟢 Connected';
    el('connStatus').className = 'status-connected';
    log('[System] Connected');
    refreshAll();
  };
  ws.onclose = () => {
    el('connStatus').innerText = '🔴 Disconnected';
    el('connStatus').className = 'status-disconnected';
    log('[System] Disconnected. Reconnecting in 3s...');
    setTimeout(connect, 3000);
  };
  ws.onerror = () => log('[System] WS Error');
  ws.onmessage = (e) => handleMessage(e.data);
}

function log(msg){
  const l = el('eventLog');
  const t = new Date().toLocaleTimeString();
  l.innerHTML = '[' + t + '] ' + msg + '<br>' + l.innerHTML;
}

function sendCmd(c){
  if (ws && ws.readyState === WebSocket.OPEN){
    ws.send(c);
    log('[Sent] ' + c);
  }
}

function refreshAll(){
  sendCmd('GET_MQTT_CFG');
  sendCmd('GET_WIFI');
  sendCmd('GET_SENSOR_LC');
  sendCmd('GET_LOOP_CFG');
  sendCmd('GET_TRAFFIC_RULES');
  sendCmd('GET_REPORT_CFG');
  sendCmd('GET_DEFAULT_KMH');
  sendCmd('GET_LOOP_GEOMETRY');
  sendCmd('GET_CPU');
  sendCmd('GET_MQTT_PUB_STATS');
}

function parseKv(payload, sep) {
  const vals = {};
  payload.split(sep).forEach(kv => {
    const x = kv.split(':');
    if (x.length === 2) vals[x[0]] = x[1];
  });
  return vals;
}

// Build LC table (8 rows: S0C0..S0C3, S1C0..S1C3)
function buildLcTable(){
  const tb = el('lcTbl');
  tb.innerHTML = '';
  for (let s=0; s<2; s++){
    for (let ch=0; ch<4; ch++){
      const row = document.createElement('tr');
      row.innerHTML = `
        <td>S${s+1}</td><td>C${ch}</td>
        <td><input type="number" id="lc_${s}_${ch}_l" value="13" step="0.1"></td>
        <td><input type="number" id="lc_${s}_${ch}_c" value="100" step="1"></td>
        <td><input type="text" id="lc_${s}_${ch}_ct" value="0x9C40" style="min-width:70px"></td>
        <td><input type="text" id="lc_${s}_${ch}_dc" value="0xA000" style="min-width:70px"></td>
        <td><button onclick="sendLC(${s},${ch})">Apply</button></td>`;
      tb.appendChild(row);
    }
  }
}
function sendLC(s, ch){
  const L = el('lc_' + s + '_' + ch + '_l').value;
  const C = el('lc_' + s + '_' + ch + '_c').value;
  sendCmd('SET_LC|' + s + '|' + ch + '|' + L + '|' + C);
}

// Build loop pair table
function buildLpTable(){
  const tb = el('lpTbl');
  tb.innerHTML = '';
  for (let i=0; i<4; i++){
    const row = document.createElement('tr');
    row.innerHTML = `
      <td>${i}</td>
      <td><select id="lp_${i}_en"><option value="1">ON</option><option value="0">OFF</option></select></td>
      <td><input type="number" id="lp_${i}_dist" value="0.4" step="0.1"></td>
      <td><select id="lp_${i}_s1"><option value="0">S1</option><option value="1">S2</option></select></td>
      <td><select id="lp_${i}_c1"><option>0</option><option>1</option><option>2</option><option>3</option></select></td>
      <td><select id="lp_${i}_s2"><option value="0">S1</option><option value="1" selected>S2</option></select></td>
      <td><select id="lp_${i}_c2"><option>0</option><option>1</option><option>2</option><option>3</option></select></td>
      <td id="lp_${i}_speed">-</td>
      <td><button class="ok" onclick="sendLp(${i})">💾 Apply</button></td>`;
    tb.appendChild(row);
  }
}
function sendLp(i){
  sendCmd('CONFIG_SET|' + i + '|' + el('lp_' + i + '_en').value + '|' + el('lp_' + i + '_dist').value +
    '|' + el('lp_' + i + '_s1').value + '|' + el('lp_' + i + '_c1').value +
    '|' + el('lp_' + i + '_s2').value + '|' + el('lp_' + i + '_c2').value);
}

function sendMqttCfg(){
  sendCmd('SET_MQTT_FULL|' + el('mq_id').value + '|' + el('mq_server').value + '|' +
    el('mq_port').value + '|' + el('mq_ip').value + '|' + el('mq_user').value + '|' +
    el('mq_pass').value + '|' + el('mq_apn').value + '|' + el('mq_topic_events').value + '|' +
    el('mq_topic_commands').value + '|' + el('mq_topic_responses').value);
}

function sendRules(){
  sendCmd('SET_RULES|' + el('rule_speed').value + '|' + el('rule_tol').value + '|' +
    el('rule_dist').value + '|' + el('rule_headway').value + '|' + el('rule_maxheadway').value + '|' +
    el('rule_straddle_ms').value + '|' + el('rule_straddle_ratio').value + '|' + el('rule_assume').value);
}

function handleMessage(d){
  if (d.startsWith('SENSOR_DATA|')) {
    // High-rate stream — ignore in log so important messages stay visible.
    return;
  } else if (d.startsWith('MQTT_CFG|')) {
    // MQTT_CFG|id|server|port|ip|user|pass|apn|topic_events|topic_commands|topic_responses
    const p = d.split('|');
    if (p.length >= 11) {
      el('mq_id').value = p[1];
      el('mq_server').value = p[2];
      el('mq_port').value = p[3];
      el('mq_ip').value = p[4];
      el('mq_user').value = p[5];
      el('mq_pass').value = p[6];
      el('mq_apn').value = p[7];
      el('mq_topic_events').value = p[8];
      el('mq_topic_commands').value = p[9];
      el('mq_topic_responses').value = p[10];
    }
  } else if (d.startsWith('WIFI|')) {
    // WIFI|ssid|ip|rssi
    const p = d.split('|');
    if (p.length >= 4) {
      el('wf_ssid').value = p[1];
      el('wf_ip').value = p[2];
      el('wf_rssi').value = p[3];
    }
  } else if (d.startsWith('SENSOR_LC|')) {
    // SENSOR_LC|s0c0:L|C|ct|dc|... per channel
    const p = d.split('|').slice(1);
    p.forEach(tok => {
      const kv = tok.split(':');
      if (kv.length === 5) {
        const id = kv[0]; // e.g. s0c0
        const m = id.match(/^s([01])c([0-3])$/);
        if (m) {
          const s = +m[1], ch = +m[2];
          el('lc_' + s + '_' + ch + '_l').value = kv[1];
          el('lc_' + s + '_' + ch + '_c').value = kv[2];
          el('lc_' + s + '_' + ch + '_ct').value = kv[3];
          el('lc_' + s + '_' + ch + '_dc').value = kv[4];
        }
      }
    });
  } else if (d.startsWith('CONFIG_ACK|')) {
    // CONFIG_ACK|idx:0|dual:1|dist:0.4|s1:0|c1:0|s2:1|c2:0
    const vals = {};
    d.substring(11).split('|').forEach(kv => {
      const x = kv.split(':');
      if (x.length === 2) vals[x[0]] = x[1];
    });
    const idx = (vals.idx !== undefined) ? parseInt(vals.idx) : 0;
    if (idx >= 0 && idx < 4) {
      if (vals.dual !== undefined) el('lp_' + idx + '_en').value = vals.dual;
      if (vals.dist !== undefined) el('lp_' + idx + '_dist').value = vals.dist;
      if (vals.s1 !== undefined) el('lp_' + idx + '_s1').value = vals.s1;
      if (vals.c1 !== undefined) el('lp_' + idx + '_c1').value = vals.c1;
      if (vals.s2 !== undefined) el('lp_' + idx + '_s2').value = vals.s2;
      if (vals.c2 !== undefined) el('lp_' + idx + '_c2').value = vals.c2;
    }
    log('✅ Loop cfg applied');
  } else if (d.startsWith('SPEED_STATE|') || d.startsWith('SPEED|')) {
    const vals = {};
    d.split('|').slice(1).forEach(kv => {
      const x = kv.split(':');
      if (x.length === 2) vals[x[0]] = x[1];
    });
    if (vals.idx !== undefined) {
      const i = parseInt(vals.idx);
      if (i >= 0 && i < 4) {
        if (d.startsWith('SPEED_STATE|') && vals.valid === '1') {
          el('lp_' + i + '_speed').innerText = (vals.speed || '0') + ' km/h';
        } else if (!d.startsWith('SPEED_STATE|')) {
          el('lp_' + i + '_speed').innerText = (vals.speed || '0') + ' km/h';
        }
      }
    }
  } else if (d.startsWith('RULES_ACK')) {
    // RULES_ACK|limit:...|tol:...
    const vals = {};
    d.split('|').slice(1).forEach(kv => {
      const x = kv.split(':');
      if (x.length === 2) vals[x[0]] = x[1];
    });
    if (vals.limit !== undefined) el('rule_speed').value = vals.limit;
    if (vals.tol !== undefined) el('rule_tol').value = vals.tol;
    if (vals.min_dist !== undefined) el('rule_dist').value = vals.min_dist;
    if (vals.min_headway !== undefined) el('rule_headway').value = vals.min_headway;
    if (vals.max_headway !== undefined) el('rule_maxheadway').value = vals.max_headway;
    if (vals.straddle_ms !== undefined) el('rule_straddle_ms').value = vals.straddle_ms;
    if (vals.straddle_ratio !== undefined) el('rule_straddle_ratio').value = vals.straddle_ratio;
    if (vals.assume_kmh !== undefined) el('rule_assume').value = vals.assume_kmh;
    log('✅ Rules applied');
  } else if (d.startsWith('REPORT_INTERVAL_ACK|')) {
    log('📝 Report interval updated');
  } else if (d.startsWith('REPORT_ENABLE_ACK') || d.startsWith('REPORT_CLEAR_ACK')) {
    log('📝 Report option updated');
  } else if (d.startsWith('REPORT_CFG|')) {
    // REPORT_CFG|enabled|interval_min|clear
    const p = d.split('|');
    if (p.length >= 4) {
      el('rpt_enable').value = p[1];
      el('rpt_interval').value = (parseInt(p[2]) / 60000) || 0;
      el('rpt_clear').value = p[3];
    }
  } else if (d.startsWith('DEFAULT_KMH|')) {
    const p = d.split('|');
    if (p.length >= 2) {
      const v = parseFloat(p[1]);
      if (!isNaN(v)) window._default_kmh = v;
    }
  } else if (d.startsWith('LOOP_GEOMETRY')) {
    log('[Geometry] ' + d.substring(0, 80) + (d.length > 80 ? '...' : ''));
    if (!d.startsWith('LOOP_GEOMETRY_ACK')) {
      const vals = {};
      d.split('|').slice(1).forEach(kv => {
        const x = kv.split(':');
        if (x.length === 2) vals[x[0]] = x[1];
      });
      if (vals.site !== undefined) el('geo_site').value = vals.site;
      if (vals.lanes !== undefined) el('geo_lanes').value = vals.lanes;
      if (vals.lane_w !== undefined) el('geo_lane_w').value = vals.lane_w;
      if (vals.speed_limit !== undefined) el('geo_speed').value = vals.speed_limit;
      if (vals.dir !== undefined) el('geo_dir').value = vals.dir;
    }
  } else if (d.startsWith('CPU_ACK|')) {
    const p = d.split('|');
    el('cpu0').innerText = p[1] || '0';
    el('cpu1').innerText = p[2] || '0';
  } else if (d.startsWith('SAVE_ALL_ACK')) {
    log('💾 Saved all configs to NVS');
  } else if (d.startsWith('MQTT_PUB_STATS|')) {
    const v = parseKv(d.substring(15), '|');
    if (v.published !== undefined) el('mp_published').innerText = v.published;
    if (v.dropped   !== undefined) el('mp_dropped').innerText   = v.dropped;
    if (v.reconnect !== undefined) el('mp_reconnect').innerText = v.reconnect;
    if (v.last_err  !== undefined) el('mp_err').innerText      = v.last_err;
  } else if (d.startsWith('MQTT_PUB_STATS_RESET_ACK')) {
    log('🧮 MQTT pub stats reset');
    el('mp_published').innerText = '0';
    el('mp_dropped').innerText   = '0';
    el('mp_reconnect').innerText = '0';
    el('mp_err').innerText       = 'none';
  } else {
    log('[Msg] ' + d);
  }
}

buildLcTable();
buildLpTable();
connect();
</script>
</body>
</html>
)RAW";

#endif // DASHBOARD_TECH_H