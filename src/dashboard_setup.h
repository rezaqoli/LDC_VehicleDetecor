#ifndef DASHBOARD_SETUP_H
#define DASHBOARD_SETUP_H

const char *DASHBOARD_SETUP_HTML = R"RAW(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LDC Device Setup</title><style>
:root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#f2f5f9;color:#16202b;font:14px system-ui,Arial,sans-serif}
.top{background:#102a43;color:#fff;padding:18px max(20px,calc((100% - 1100px)/2));display:flex;justify-content:space-between;align-items:center;gap:16px}
.top h1{font-size:20px;margin:0}.top a{color:#d7edff;margin-left:12px}
.wrap{max-width:1100px;margin:20px auto;padding:0 16px}
.status,.card{background:#fff;border-radius:10px;box-shadow:0 2px 9px #1d33421a}
.status{display:flex;flex-wrap:wrap;gap:9px;padding:12px;margin-bottom:16px}
.status span{background:#e9f0f7;border-radius:6px;padding:5px 8px;font-size:12px}
.up{color:#147a45;font-weight:bold}.down{color:#b42318;font-weight:bold}
.card{padding:18px;margin:14px 0}.card h2{font-size:16px;margin:0 0 12px}
.hint{color:#607080;font-size:12px}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:9px;margin:8px 0}
label{font-weight:650;font-size:12px}
input,select{padding:7px;border:1px solid #bdc9d5;border-radius:6px;min-width:66px}
button{border:0;background:#1769aa;color:#fff;border-radius:6px;padding:8px 11px;cursor:pointer}
button:hover{background:#105587}.save{background:#18864b}.danger{background:#b42318}
table{border-collapse:collapse;width:100%;font-size:12px}
th,td{border-bottom:1px solid #dce4ec;padding:7px;text-align:center}
th{background:#f4f7fa}td input,td select{max-width:82px;min-width:48px;padding:5px}
.log{height:130px;overflow:auto;background:#102a43;color:#daf1e3;border-radius:7px;padding:9px;font:12px ui-monospace,Consolas,monospace}
</style></head><body>
<header class="top"><h1>LDC1614 Device Setup</h1><nav><a href="/dev">Engineer</a><a href="/tech">Technician</a><a href="/setup">Setup</a><a href="/mqtt">Monitor</a></nav></header>
<main class="wrap">
  <div class="status">
    <span>Console: <b id="connStatus" class="down">Disconnected</b></span>
    <span>Wi-Fi: <b id="status_wifi">-</b></span>
    <span>Modem: <b id="status_modem">-</b></span>
    <span>Network: <b id="status_network">-</b></span>
    <span>Data: <b id="status_data">-</b></span>
    <span>MQTT: <b id="status_mqtt">-</b></span>
  </div>

  <section class="card"><h2>Setup actions</h2>
    <div class="row">
      <button onclick="refresh()">Refresh configuration</button>
      <button class="save" onclick="send('SAVE_ALL')">Save all to device</button>
      <button class="danger" onclick="if(confirm('Restart the device now?'))send('RESET')">Restart</button>
    </div>
  </section>

  <section class="card"><h2>Wi-Fi</h2>
    <div class="row">
      <label>SSID</label><input id="ssid" readonly>
      <label>IP address</label><input id="ip" readonly>
      <label>Signal (RSSI)</label><input id="rssi" readonly>
    </div>
    <p class="hint">Wi-Fi credentials are configured at build/deployment time and are not displayed or changed here.</p>
  </section>

  <section class="card"><h2>Sensor calibration</h2>
    <p class="hint">Set inductance and capacitance for each connected loop, then apply the row.</p>
    <table><thead><tr><th>Sensor</th><th>Channel</th><th>L (uH)</th><th>C (pF)</th><th>Apply</th></tr></thead><tbody id="lc"></tbody></table>
  </section>

  <section class="card"><h2>Loop pairs</h2>
    <p class="hint">Default lanes are S1C0/S1C1, S1C2/S1C3, S2C0/S2C1, and S2C2/S2C3.</p>
    <table><thead><tr><th>Lane</th><th>Enabled</th><th>Spacing (m)</th><th>Loop A</th><th>Loop B</th><th>Apply</th></tr></thead><tbody id="pairs"></tbody></table>
  </section>

  <section class="card"><h2>Periodic reports</h2>
    <div class="row">
      <label>Interval (minutes)</label><input id="interval" type="number" min="0" value="5">
      <button class="save" onclick="send('SET_REPORT_INTERVAL|'+el('interval').value)">Apply interval</button>
      <label>Automatic reports</label><select id="enabled"><option value="1">On</option><option value="0">Off</option></select>
      <button onclick="send('SET_REPORT_ENABLE|'+el('enabled').value)">Apply</button>
      <label>Clear after report</label><select id="clear"><option value="0">No</option><option value="1">Yes</option></select>
      <button onclick="send('SET_REPORT_CLEAR|'+el('clear').value)">Apply</button>
    </div>
  </section>

  <section class="card"><h2>Firmware update over modem</h2>
    <p class="hint">Host the PlatformIO firmware.bin on an HTTP server. Include its lowercase MD5 checksum so the board can reject a damaged or unexpected image. The device restarts only after a complete validated download.</p>
    <div class="row">
      <label>Firmware URL</label><input id="otaUrl" type="url" placeholder="http://server/firmware.bin" style="min-width:320px">
      <label>MD5</label><input id="otaMd5" pattern="[A-Fa-f0-9]{32}" placeholder="32 hexadecimal characters" style="min-width:210px">
      <button class="save" onclick="startOta()">Start modem OTA</button>
    </div>
  </section>

  <section class="card"><h2>Site geometry</h2>
    <div class="row">
      <label>Site ID</label><input id="site" value="SITE-01">
      <label>Lanes</label><input id="lanes" type="number" min="1" max="8" value="4">
      <label>Lane width (m)</label><input id="width" type="number" step=".1" value="3.5">
      <label>Speed limit</label><input id="speed" type="number" value="100">
      <label>Direction</label><input id="dir" type="number" value="0">
      <button class="save" onclick="saveGeometry()">Apply geometry</button>
    </div>
  </section>

  <section class="card"><h2>Activity</h2><div class="log" id="log">Ready</div></section>
</main>
<script>
let ws;
const el = id => document.getElementById(id);
const defaults = [[0,0,0,1],[0,2,0,3],[1,0,1,1],[1,2,1,3]];

function log(s){
  const t = new Date().toLocaleTimeString();
  el('log').innerHTML = '[' + t + '] ' + s + '<br>' + el('log').innerHTML;
}
function send(s){
  if (ws && ws.readyState === 1){ ws.send(s); log('Sent: ' + s); }
}
function options(selected){
  return [0,1,2,3].map(n => '<option value="'+n+'" '+(n===selected?'selected':'')+'>'+n+'</option>').join('');
}

/* ---------- Build tables (same logic as dashboard_tech) ---------- */
function build(){
  let h = '';
  for (let s = 0; s < 2; s++)
    for (let c = 0; c < 4; c++)
      h += '<tr><td>S'+(s+1)+'</td><td>C'+c+'</td>'
        +  '<td><input id="l'+s+c+'" type="number" value="13" step=".1"></td>'
        +  '<td><input id="c'+s+c+'" type="number" value="330"></td>'
        +  '<td><button onclick="setLc('+s+','+c+')">Apply</button></td></tr>';
  el('lc').innerHTML = h;

  h = '';
  defaults.forEach((d,i) => {
    h += '<tr><td>'+(i+1)+'</td>'
      +  '<td><select id="en'+i+'"><option value="0">Off</option><option value="1">On</option></select></td>'
      +  '<td><input id="dist'+i+'" type="number" value="0.4" step=".1"></td>'
      +  '<td><select id="s1'+i+'"><option value="0" '+(d[0]===0?'selected':'')+'>S1</option><option value="1" '+(d[0]===1?'selected':'')+'>S2</option></select>'
      +  ' C<select id="c1'+i+'">'+options(d[1])+'</select></td>'
      +  '<td><select id="s2'+i+'"><option value="0" '+(d[2]===0?'selected':'')+'>S1</option><option value="1" '+(d[2]===1?'selected':'')+'>S2</option></select>'
      +  ' C<select id="c2'+i+'">'+options(d[3])+'</select></td>'
      +  '<td><button class="save" onclick="setPair('+i+')">Apply</button></td></tr>';
  });
  el('pairs').innerHTML = h;
}

function setLc(s,c){ send('SET_LC|'+s+'|'+c+'|'+el('l'+s+c).value+'|'+el('c'+s+c).value); }
function setPair(i){
  send('CONFIG_SET|'+i+'|'+el('en'+i).value+'|'+el('dist'+i).value
     + '|'+el('s1'+i).value+'|'+el('c1'+i).value
     + '|'+el('s2'+i).value+'|'+el('c2'+i).value);
}
function startOta(){
  let u = el('otaUrl').value.trim(), m = el('otaMd5').value.trim();
  if (!/^http:\/\/[^|]+$/i.test(u) || !/^[a-f0-9]{32}$/i.test(m)){
    log('OTA requires a valid http URL and 32-character MD5'); return;
  }
  if (confirm('Download firmware over the modem and restart this device?'))
    send('OTA_UPDATE|'+u+'|'+m);
}
function saveGeometry(){
  send('SET_LOOP_GEOMETRY|'+el('site').value+'|'+el('lanes').value+'|'
     + el('width').value+'|'+el('speed').value+'|'+el('dir').value);
}
function refresh(){
  ['GET_SYSTEM_STATUS','GET_WIFI','GET_SENSOR_LC','GET_LOOP_CFG',
   'GET_REPORT_CFG','GET_LOOP_GEOMETRY'].forEach(send);
}

/* ---------- Message parser (mirrors dashboard_tech) ---------- */
function updateSystemStatus(msg){
  const values = {};
  msg.substring('SYSTEM_STATUS|'.length).split('|').forEach(part => {
    const i = part.indexOf(':');
    if (i > 0) values[part.substring(0,i)] = part.substring(i+1);
  });
  ['wifi','modem','network','data','mqtt'].forEach(key => {
    const node = el('status_' + key);
    if (!node || values[key] === undefined) return;
    node.textContent = values[key];
    node.className = (values[key] === 'UP') ? 'up'
                    : (values[key] === 'UNKNOWN') ? '' : 'down';
  });
}

function parseEvent(data){
  const parts = data.split('|');
  if (parts.length < 2) return;

  const channel = parts[1] || 'unknown';
  const values = {};
  if (parts.length > 2 && parts[2].indexOf(':') > 0) {
    for (let i = 2; i < parts.length; i++) {
      const sep = parts[i].indexOf(':');
      if (sep > 0) values[parts[i].substring(0, sep)] = parts[i].substring(sep + 1);
    }
  } else if (parts.length >= 7) {
    values.class = parts[2];
    values.dur = parts[3];
    values.peak = parts[4];
    values.len = parts[5];
    values.peaks = parts[6];
  } else {
    return;
  }

  const vehicleClass = values.class || values.vehicleClass || values.cls || '--';
  const duration = values.dur || '--';
  const length = values.len || '--';
  log('[Event] ' + channel + ' class=' + vehicleClass + ' dur=' + duration + 'ms len=' + length + 'm');
}

function parseSpeedMessage(data){
  const parts = data.split('|');
  const values = {};
  if (parts.length > 1 && parts[1].indexOf(':') < 0 && !data.startsWith('SPEED_STATE|')) {
    values.idx = parts[1]; values.speed = parts[2]; values.len = parts[3];
    values.type = parts[4]; values.delay = parts[5]; values.dir = parts[6];
  } else {
    for (let i = 1; i < parts.length; i++) {
      const sep = parts[i].indexOf(':');
      if (sep > 0) values[parts[i].substring(0, sep)] = parts[i].substring(sep + 1);
    }
  }

  const idx = Number(values.idx);
  if (!Number.isInteger(idx) || idx < 0 || idx > 3) return;

  const speed = values.speed || '0';
  if (!data.startsWith('SPEED_STATE|')) {
    log('🏁 SPEED pair ' + idx + ': ' + speed + ' km/h, ' +
      (values.type || '--') + ', len=' + (values.len || '0') + 'm');
  }
}

function parse(msg){
  if (msg.startsWith('SYSTEM_STATUS|')){
    updateSystemStatus(msg);
  } else if (msg.startsWith('SENSOR_DATA|')){
    return;
  } else if (msg.startsWith('EVENT|')){
    parseEvent(msg);
  } else if (msg.startsWith('SPEED|') || msg.startsWith('SPEED_STATE|')){
    parseSpeedMessage(msg);
  } else if (msg.startsWith('WIFI|')){
    const p = msg.split('|');
    el('ssid').value = p[1] || '-';
    el('ip').value   = p[2] || '-';
    el('rssi').value = p[3] || '-';
  } else if (msg.startsWith('SENSOR_LC|')){
    msg.split('|').slice(1).forEach(tok => {
      const kv = tok.split(':');
      const m = /^s([01])c([0-3])$/.exec(kv[0]);
      if (m && kv.length >= 3){
        el('l'+m[1]+m[2]).value = kv[1];
        el('c'+m[1]+m[2]).value = kv[2];
      }
    });
  } else if (msg.startsWith('CONFIG_ACK|')){
    const v = {};
    msg.substring('CONFIG_ACK|'.length).split('|').forEach(kv => {
      const p = kv.split(':');
      if (p.length === 2) v[p[0]] = p[1];
    });
    const i = parseInt(v.idx);
    if (i >= 0 && i < 4){
      if (v.dual !== undefined) el('en'+i).value   = v.dual;
      if (v.dist !== undefined) el('dist'+i).value = v.dist;
      if (v.s1   !== undefined) el('s1'+i).value   = v.s1;
      if (v.c1   !== undefined) el('c1'+i).value   = v.c1;
      if (v.s2   !== undefined) el('s2'+i).value   = v.s2;
      if (v.c2   !== undefined) el('c2'+i).value   = v.c2;
    }
    log('Loop cfg applied');
  } else if (msg.startsWith('REPORT_CFG|')){
    const p = msg.split('|');
    el('enabled').value  = p[1];
    el('interval').value = (parseInt(p[2]) / 60000) || 5;
    el('clear').value    = p[3];
  } else if (msg.startsWith('LOOP_GEOMETRY|') && !msg.startsWith('LOOP_GEOMETRY_ACK')){
    const v = {};
    msg.split('|').slice(1).forEach(kv => {
      const p = kv.split(':');
      if (p.length === 2) v[p[0]] = p[1];
    });
    if (v.site        !== undefined) el('site').value  = v.site;
    if (v.lanes       !== undefined) el('lanes').value = v.lanes;
    if (v.lane_w      !== undefined) el('width').value = v.lane_w;
    if (v.speed_limit !== undefined) el('speed').value = v.speed_limit;
    if (v.dir         !== undefined) el('dir').value   = v.dir;
  } else {
    log('[Msg] ' + msg);
  }
}

/* ---------- WebSocket connection (mirrors dashboard_tech) ---------- */
function connect(){
  ws = new WebSocket('ws://' + location.hostname + ':81/');

  ws.onopen = () => {
    el('connStatus').textContent = 'Connected';
    el('connStatus').className = 'up';
    log('[System] Connected');

    // Wire handlers BEFORE sending any request, so replies are never lost.
    ws.onmessage = e => parse(e.data);
    ws.onerror   = () => log('[System] WS Error');
    ws.onclose   = () => {
      el('connStatus').textContent = 'Disconnected';
      el('connStatus').className = 'down';
      log('[System] Disconnected. Reconnecting in 3s...');
      setTimeout(connect, 3000);
    };

    // Now it is safe to request configuration.
    refresh();
  };
}

build();
connect();
</script></body></html>
)RAW";

#endif
