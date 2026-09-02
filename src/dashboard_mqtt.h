#ifndef DASHBOARD_MQTT_H
#define DASHBOARD_MQTT_H

const char* DASHBOARD_MQTT_HTML = R"RAW(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>LDC1614 MQTT Monitor</title>
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
.status-connected { color: #198754; font-weight: bold; }
.status-disconnected { color: #dc3545; font-weight: bold; }
.event-log { height: 320px; overflow-y: auto; background: #13212f; color: #8ff4a2; padding: 10px; font-family: Consolas, monospace; font-size: 12px; border-radius: 8px; }
.event-log .in  { color: #8ff4a2; }
.event-log .out { color: #79c0ff; }
.event-log .err { color: #ff7b7b; }
.event-log .sys { color: #ffd966; }
.small { font-size: 12px; color: #555; }
.kv { display: inline-block; padding: 2px 8px; margin: 2px; border-radius: 4px; background: #eef3f8; font-size: 12px; }
.nav a { margin-right: 10px; font-size: 13px; }
table { width: 100%; border-collapse: collapse; margin-top: 8px; font-size: 13px; }
th, td { border: 1px solid #d9dde2; padding: 7px; text-align: center; }
th { background: #eef3f8; }
</style>
</head>
<body>
<h1>📡 LDC1614 MQTT Monitor</h1>
<p class="nav">
  <a href="/dev">/dev (Engineer)</a>
  <a href="/tech">/tech (Technician)</a>
  <a href="/mqtt">/mqtt (Monitor)</a>
</p>
<p>Connection: <span id="connStatus" class="status-disconnected">⚪ Disconnected</span></p>

<div class="card">
  <h2>📊 Broker Status</h2>
  <div class="row">
    <span class="kv">Broker: <b id="mqtt_broker">-</b></span>
    <span class="kv">Port: <b id="mqtt_port">-</b></span>
    <span class="kv">Client ID: <b id="mqtt_id">-</b></span>
    <span class="kv">State: <b id="mqtt_state">-</b></span>
    <span class="kv">LTE: <b id="mqtt_lte">-</b></span>
  </div>
  <div class="row" style="margin-top:6px;">
    <span class="kv">Topic Events: <b id="mqtt_t_evt">-</b></span>
    <span class="kv">Topic Commands: <b id="mqtt_t_cmd">-</b></span>
    <span class="kv">Topic Responses: <b id="mqtt_t_resp">-</b></span>
  </div>
  <div class="row" style="margin-top:10px;">
    <button onclick="sendCmd('GET_MQTT_CFG')">Refresh Status</button>
    <button onclick="sendCmd('GET_MQTT_ID')">Get ID</button>
    <button onclick="sendCmd('GET_MQTT_SERVER')">Get Server</button>
    <button class="danger" onclick="el('log').innerHTML='[System] Cleared'">Clear Log</button>
  </div>
</div>

<div class="card">
  <h2>📨 Live MQTT &amp; Event Log</h2>
  <p class="small">Lines tagged <b>[OUT]</b> are messages the ESP is publishing; <b>[IN]</b> are commands received from the broker; <b>[ERR]</b> are broker/network errors; <b>[SYS]</b> are local status events.</p>
  <div class="row" style="margin-bottom:6px;">
    <label>Filter:</label>
    <select id="filter">
      <option value="all">All</option>
      <option value="in">IN only</option>
      <option value="out">OUT only</option>
      <option value="err">ERR only</option>
      <option value="sys">SYS only</option>
    </select>
    <label>Search:</label><input type="text" id="q" placeholder="substring..." style="min-width:140px">
    <label>Auto-scroll:</label><input type="checkbox" id="autoscroll" checked>
    <label>Max lines:</label><input type="number" id="maxlines" value="500" min="50" max="5000" step="50" style="width:90px">
  </div>
  <div id="log" class="event-log">[System] Ready</div>
</div>

<div class="card">
  <h2>📝 Publish Test (ESP → broker)</h2>
  <div class="row">
    <label>Topic:</label><input type="text" id="pub_topic" value="vehicles/events" style="min-width:200px">
    <label>Payload:</label><input type="text" id="pub_payload" value='{"test":1}' style="min-width:260px">
    <button onclick="publishTest()">Send (via WS broadcast → will appear in log when echoed)</button>
  </div>
  <p class="small">Note: direct MQTT publish from the browser is not possible — this sends a WS command for local debug; for real MQTT publish use the ESP's normal event flow.</p>
</div>

<div class="card">
  <h2>📈 Traffic Statistics (quick view)</h2>
  <div class="row">
    <button onclick="sendCmd('GET_REPORT')">Get Report</button>
    <button onclick="sendCmd('GET_REPORT|1')">Get Report + Clear</button>
    <button class="danger" onclick="sendCmd('RESET_STATS')">Reset Stats</button>
  </div>
  <div id="trafficReport" style="margin-top:10px; padding:10px; background:#e8f4f8; border-radius:6px; font-size:13px; display:none;"></div>
</div>

<script>
let ws;
const IP = window.location.hostname;
const buffer = [];
let renderScheduled = false;

function el(id){ return document.getElementById(id); }

function connect(){
  ws = new WebSocket('ws://' + IP + ':81/');
  ws.onopen = () => {
    el('connStatus').innerText = '🟢 Connected';
    el('connStatus').className = 'status-connected';
    pushLine('sys', '[SYS] Connected to WebSocket');
    sendCmd('GET_MQTT_CFG');
    sendCmd('GET_REPORT');
  };
  ws.onclose = () => {
    el('connStatus').innerText = '🔴 Disconnected';
    el('connStatus').className = 'status-disconnected';
    pushLine('sys', '[SYS] Disconnected. Reconnecting in 3s...');
    setTimeout(connect, 3000);
  };
  ws.onerror = () => pushLine('err', '[ERR] WS error');
  ws.onmessage = (e) => handleMessage(e.data);
}

function pushLine(kind, text){
  buffer.push({ kind, text, t: new Date() });
  scheduleRender();
}

function scheduleRender(){
  if (renderScheduled) return;
  renderScheduled = true;
  requestAnimationFrame(() => {
    renderScheduled = false;
    renderLog();
  });
}

function renderLog(){
  const filt = el('filter').value;
  const q = (el('q').value || '').toLowerCase();
  const max = parseInt(el('maxlines').value) || 500;
  let html = '';
  let shown = 0;
  for (let i = buffer.length - 1; i >= 0 && shown < max; i--) {
    const { kind, t } = buffer[i];
    const txt = buffer[i].text;
    if (filt !== 'all' && filt !== kind) continue;
    if (q && txt.toLowerCase().indexOf(q) === -1) continue;
    const ts = t.toLocaleTimeString();
    html += `<span class="${kind}">[${ts}] ${escapeHtml(txt)}</span><br>`;
    shown++;
  }
  el('log').innerHTML = html || '[System] (no matching lines)';
}

function escapeHtml(s){
  return s.replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
}

function sendCmd(c){
  if (ws && ws.readyState === WebSocket.OPEN){
    ws.send(c);
    pushLine('out', '[OUT→WS] ' + c);
  }
}

function publishTest(){
  // Just push a marker to the log; actual MQTT publish still needs ESP flow
  const t = el('pub_topic').value;
  const p = el('pub_payload').value;
  pushLine('out', '[TEST] would publish to ' + t + ' : ' + p);
}

function handleMessage(d){
  // SENSOR_DATA is high-rate raw telemetry — not relevant on the MQTT panel.
  if (d.startsWith('SENSOR_DATA|')) return;

  // Only show messages relevant to the MQTT / LTE / system view.
  const isRelevant =
       d.startsWith('EVENT|')
    || d.startsWith('TRAFFIC_REPORT')
    || d.startsWith('SPEED|') || d.startsWith('SPEED_STATE|')
    || d.startsWith('MQTT_')
    || d.startsWith('CONFIG_ACK')
    || d.startsWith('RULES_ACK')
    || d.startsWith('REPORT_')
    || d.startsWith('LOOP_GEOMETRY')
    || d.startsWith('LC_ACK')
    || d.startsWith('THRESHOLD_ACK')
    || d.startsWith('EVENT_RANGE_ACK')
    || d.startsWith('CLASSIFY_ACK')
    || d.startsWith('DETECTOR_ACK')
    || d.startsWith('AUTO_THRESH_ACK')
    || d.startsWith('DEFAULT_KMH_ACK')
    || d.startsWith('ADJACENT_ACK')
    || d.startsWith('CALIBRATION_STARTED')
    || d.startsWith('SAVE_ALL_ACK')
    || d.startsWith('STATS_RESET_ACK')
    || d.startsWith('STATUS')
    || d.startsWith('NOISE')
    || d.startsWith('CPU_ACK')
    || d.startsWith('CALIB_STATUS')
    || d.startsWith('CONFIG_')
    || d.startsWith('ERROR');

  if (!isRelevant) {
    pushLine('sys', '[Filtered] ' + d.substring(0, 120));
    return;
  }

  if (d.startsWith('EVENT|')) {
    pushLine('out', '[OUT→MQTT] ' + d);
  } else if (d.startsWith('TRAFFIC_REPORT')) {
    pushLine('out', '[OUT→MQTT] ' + d);
    parseTrafficReport(d);
  } else if (d.startsWith('MQTT_CFG|')) {
    parseMqttCfg(d);
    pushLine('sys', '[SYS] MQTT cfg received');
  } else if (d.startsWith('MQTT_ID|')) {
    el('mqtt_id').innerText = d.substring(8);
    pushLine('sys', '[SYS] ID: ' + d);
  } else if (d.startsWith('MQTT_SERVER_ACK')) {
    pushLine('sys', '[SYS] MQTT server saved');
  } else if (d.startsWith('ERROR')) {
    pushLine('err', '[ERR] ' + d);
  } else if (d.startsWith('SPEED|') || d.startsWith('SPEED_STATE|')) {
    pushLine('out', '[OUT→MQTT] ' + d);
  } else {
    pushLine('sys', '[SYS] ' + d);
  }
}

function parseMqttCfg(d){
  // MQTT_CFG|id|server|port|ip|user|pass|apn|topic_events|topic_commands|topic_responses
  const p = d.split('|');
  if (p.length >= 11) {
    el('mqtt_broker').innerText = p[2] + ' (' + p[4] + ')';
    el('mqtt_port').innerText = p[3];
    el('mqtt_id').innerText = p[1];
    el('mqtt_t_evt').innerText = p[8];
    el('mqtt_t_cmd').innerText = p[9];
    el('mqtt_t_resp').innerText = p[10];
  }
}

function parseTrafficReport(d){
  const div = el('trafficReport');
  div.style.display = 'block';
  const parts = d.split('|');
  let html = '<b>Traffic Report</b><br>';
  const labels = {
    dur_s: 'Duration', total: 'Total Vehicles', avg_speed: 'Avg Speed',
    speed_viol: 'Speed Violations', dist_viol: 'Distance Violations', lane_viol: 'Lane Violations'
  };
  for (let i = 1; i < parts.length; i++) {
    const kv = parts[i].split(':');
    if (kv.length !== 2) continue;
    const key = kv[0], val = kv[1];
    if (labels[key]) {
      html += '<b>' + labels[key] + ':</b> ' + val;
      if (key === 'avg_speed') html += ' km/h';
      if (key === 'dur_s') html += 's';
      html += '<br>';
    } else if (key.endsWith('_cnt')) {
      const cls = key.replace('_cnt', '');
      html += '<b>' + cls + ':</b> ' + val + ' vehicles';
    } else if (key.endsWith('_avg')) {
      const cls = key.replace('_avg', '');
      html += ' (avg ' + val + ' km/h)<br>';
    }
  }
  div.innerHTML = html;
}

el('filter').addEventListener('change', renderLog);
el('q').addEventListener('input', renderLog);
el('maxlines').addEventListener('change', renderLog);

connect();
</script>
</body>
</html>
)RAW";

#endif // DASHBOARD_MQTT_H