#include "html_pages.h"

// TODO: Etap 5b — pełny dashboard z Chart.js, widok temperatury, historii, konfiguracji

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermo Control</title>
<style>
  body { font-family: sans-serif; background:#1a1a2e; color:#e0e0e0; margin:0; padding:16px; }
  h1   { color:#4fc3f7; }
  h2   { color:#4fc3f7; font-size:1.05em; margin:0 0 8px; }
  .card { background:#16213e; border-radius:8px; padding:16px; margin:8px 0; }
  .temp { font-size:3em; color:#4fc3f7; }
  .badge { display:inline-block; padding:4px 10px; border-radius:12px; font-size:.85em; }
  .idle    { background:#2e7d32; }
  .heating { background:#e65100; }
  .cooling { background:#0277bd; }
  .alarm   { background:#b71c1c; }
  .row  { margin:6px 0; }
  .row label { display:inline-block; width:220px; font-size:.9em; }
  .row input { width:100px; padding:4px; border-radius:4px; border:none; }
  button { padding:6px 12px; border-radius:4px; border:none; background:#4fc3f7; cursor:pointer; margin:4px 4px 4px 0; }
  button.danger { background:#ef5350; }
  table { border-collapse: collapse; width:100%; font-size:.85em; }
  th, td { text-align:left; padding:4px 8px; border-bottom:1px solid #2a3a5e; }
  #err_thermo, #err_logzone { color:#ef5350; font-size:.85em; }
</style>
</head>
<body>
<h1>Thermo Control</h1>

<div class="card">
  <div class="temp" id="temp">--.-°C</div>
  <span class="badge idle" id="state">IDLE</span>
  &nbsp;Grzałka: <span id="heater">OFF</span>
  &nbsp;Wentylator: <span id="fan">0%</span>
  &nbsp;Alarmy: <span id="alarms">brak</span>
</div>

<div class="card">
  <b>Energia:</b> dziś: <span id="energy_today">0.000</span> kWh
  &nbsp;|&nbsp; moc: <span id="power">0</span> W
</div>

<div class="card">
  <h2>System</h2>
  <small id="sysdis" style="color:#ffb300;"></small><br>
  <button id="btnSystemToggle">Włącz / wyłącz system</button>
  <button id="btnAck">Potwierdź alarm</button>
  <button id="btnSystemReset" class="danger">Reset automatu stanów</button>
  <button id="btnLogout">Wyloguj</button>
</div>

<div class="card">
  <h2>Test: sterowanie wentylatorem ręczne</h2>
  <input type="range" id="fanSlider" min="0" max="100" value="0" style="width:70%;vertical-align:middle;">
  <span id="fanSliderVal">0</span>%
  <button id="fanSet">Ustaw</button>
</div>

<div class="card">
  <h2>Konfiguracja algorytmu (ThermoConfig)</h2>
  <div class="row"><label for="target_temp">Temp. zadana [°C]</label><input type="number" step="0.1" id="target_temp"></div>
  <div class="row"><label for="heat_hyst">Histereza grzałki (ON poniżej T−X) [°C]</label><input type="number" step="0.1" id="heat_hyst"></div>
  <div class="row"><label for="cool_start">Start wentylatora (T+X) [°C]</label><input type="number" step="0.1" id="cool_start"></div>
  <div class="row"><label for="cool_full">Wentylator 100% (T+X) [°C]</label><input type="number" step="0.1" id="cool_full"></div>
  <div class="row"><label for="hysteresis_band">Pasmo wyłączenia grzałki [°C]</label><input type="number" step="0.1" id="hysteresis_band"></div>
  <div class="row"><label for="alarm_delta_low">Próg ALARM_LOW (T−X) [°C]</label><input type="number" step="0.1" id="alarm_delta_low"></div>
  <div class="row"><label for="alarm_delta_high">Próg ALARM_HIGH (T+X) [°C]</label><input type="number" step="0.1" id="alarm_delta_high"></div>
  <div class="row"><label for="trend_alarm_delta">Próg alarmu trendu (spadek/5min) [°C]</label><input type="number" step="0.1" id="trend_alarm_delta"></div>
  <div class="row"><label for="temp_offset">Kalibracja czujnika [°C]</label><input type="number" step="0.1" id="temp_offset"></div>
  <div class="row"><label for="heater_watt">Moc grzałki [W]</label><input type="number" step="1" id="heater_watt"></div>
  <div class="row"><label for="pulses_per_kwh">Impulsy / kWh (miernik energii)</label><input type="number" step="1" id="pulses_per_kwh"></div>
  <button id="btnSaveThermo">Zapisz</button>
  <div id="err_thermo"></div>
</div>

<div class="card">
  <h2>Progi logowania zdarzeniowego (LogZoneConfig)</h2>
  <div class="row"><label for="normal_delta">Strefa prawidłowa (±) [°C]</label><input type="number" step="0.1" id="normal_delta"></div>
  <div class="row"><label for="low_step_1">Poniżej target — stopień 1 [°C]</label><input type="number" step="0.1" id="low_step_1"></div>
  <div class="row"><label for="low_step_2">Poniżej target — stopień 2 [°C]</label><input type="number" step="0.1" id="low_step_2"></div>
  <div class="row"><label for="low_step_3">Poniżej target — stopień 3 [°C]</label><input type="number" step="0.1" id="low_step_3"></div>
  <div class="row"><label for="high_step_1">Powyżej target — stopień 1 [°C]</label><input type="number" step="0.1" id="high_step_1"></div>
  <div class="row"><label for="high_step_2">Powyżej target — stopień 2 [°C]</label><input type="number" step="0.1" id="high_step_2"></div>
  <div class="row"><label for="high_step_3">Powyżej target — stopień 3 [°C]</label><input type="number" step="0.1" id="high_step_3"></div>
  <button id="btnSaveLogZone">Zapisz</button>
  <div id="err_logzone"></div>
</div>

<div class="card">
  <h2>Historia zdarzeń (ring buffer FRAM, najnowsze pierwsze)</h2>
  <button id="btnLoadHistory">Odśwież historię</button>
  <table>
    <thead><tr><th>Czas</th><th>Temp</th><th>Strefa</th><th>Powód</th><th>Niestabilna</th><th>Grzałka</th><th>Fan</th></tr></thead>
    <tbody id="historyBody"></tbody>
  </table>
</div>

<script>
const ZONE_NAMES = ['IDLE','HEATING','COOLING','ALARM_LOW_1','ALARM_LOW_2','ALARM_LOW_3',
                     'ALARM_HIGH_1','ALARM_HIGH_2','ALARM_HIGH_3','TREND_ALARM','SENSOR_FAULT'];
const REASON_NAMES = ['ZONE_CHANGE','HEARTBEAT','BOOT'];

async function refresh() {
  try {
    const r = await fetch('/api/status');
    if (r.status === 401) { window.location.href = '/login'; return; }
    const d = await r.json();
    document.getElementById('temp').textContent = d.temp_c.toFixed(1) + '°C';
    document.getElementById('state').textContent = d.state;
    document.getElementById('heater').textContent = d.heater_on ? 'ON' : 'OFF';
    document.getElementById('fan').textContent = d.fan_pct + '%';
    document.getElementById('sysdis').textContent = d.system_disabled
      ? '⏸ System wstrzymany — ręczne sterowanie aktywne' : '▶ Algorytm aktywny — nadpisze ręczne ustawienie';

    const flags = [];
    if (d.alarm_flags & 0x01) flags.push('TEMP_LOW');
    if (d.alarm_flags & 0x02) flags.push('TEMP_HIGH');
    if (d.alarm_flags & 0x04) flags.push('TREND');
    if (d.alarm_flags & 0x08) flags.push('SENSOR');
    document.getElementById('alarms').textContent = flags.length ? flags.join(', ') : 'brak';

    const e = await fetch('/api/energy');
    const en = await e.json();
    document.getElementById('energy_today').textContent = en.today_kwh.toFixed(3);
    document.getElementById('power').textContent = en.power_w.toFixed(0);
  } catch(err) { console.error(err); }
}

async function loadThermoConfig() {
  const r = await fetch('/api/thermo-config');
  const c = await r.json();
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','hysteresis_band',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','temp_offset',
                    'heater_watt','pulses_per_kwh']) {
    if (c[k] !== undefined) document.getElementById(k).value = c[k];
  }
}

async function loadLogZoneConfig() {
  const r = await fetch('/api/log-zone-config');
  const c = await r.json();
  for (const k of ['normal_delta','low_step_1','low_step_2','low_step_3',
                    'high_step_1','high_step_2','high_step_3']) {
    if (c[k] !== undefined) document.getElementById(k).value = c[k];
  }
}

async function loadHistory() {
  const r = await fetch('/api/history');
  const d = await r.json();
  const tbody = document.getElementById('historyBody');
  tbody.innerHTML = '';
  for (const rec of d.records) {
    const tr = document.createElement('tr');
    const time = new Date(rec.ts * 1000).toLocaleString();
    tr.innerHTML = `<td>${time}</td><td>${rec.temp_c.toFixed(1)}°C</td>` +
      `<td>${ZONE_NAMES[rec.zone] ?? rec.zone}</td><td>${REASON_NAMES[rec.reason] ?? rec.reason}</td>` +
      `<td>${rec.unstable ? '⚠️' : ''}</td><td>${rec.heater_on ? 'ON' : 'OFF'}</td><td>${rec.fan_pct}%</td>`;
    tbody.appendChild(tr);
  }
}

document.getElementById('fanSlider').addEventListener('input', (e) => {
  document.getElementById('fanSliderVal').textContent = e.target.value;
});
document.getElementById('fanSet').addEventListener('click', async () => {
  const pct = document.getElementById('fanSlider').value;
  await fetch('/api/test/fan', { method: 'POST', body: new URLSearchParams({pct}) });
  refresh();
});

document.getElementById('btnSystemToggle').addEventListener('click', async () => {
  await fetch('/api/system-toggle', { method: 'POST' });
  refresh();
});
document.getElementById('btnAck').addEventListener('click', async () => {
  await fetch('/api/reset-alarm', { method: 'POST' });
  refresh();
});
document.getElementById('btnSystemReset').addEventListener('click', async () => {
  if (!confirm('Zresetować automat stanów?')) return;
  await fetch('/api/system-reset', { method: 'POST' });
  refresh();
});
document.getElementById('btnLogout').addEventListener('click', async () => {
  await fetch('/api/logout', { method: 'POST' });
  window.location.href = '/login';
});

document.getElementById('btnSaveThermo').addEventListener('click', async () => {
  const body = {};
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','hysteresis_band',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','temp_offset',
                    'heater_watt','pulses_per_kwh']) {
    body[k] = document.getElementById(k).value;
  }
  const r = await fetch('/api/thermo-config', { method: 'POST', body: new URLSearchParams(body) });
  const j = await r.json();
  document.getElementById('err_thermo').textContent = j.ok ? '' : (j.error || 'Błąd zapisu');
});

document.getElementById('btnSaveLogZone').addEventListener('click', async () => {
  const body = {};
  for (const k of ['normal_delta','low_step_1','low_step_2','low_step_3',
                    'high_step_1','high_step_2','high_step_3']) {
    body[k] = document.getElementById(k).value;
  }
  const r = await fetch('/api/log-zone-config', { method: 'POST', body: new URLSearchParams(body) });
  const j = await r.json();
  document.getElementById('err_logzone').textContent = j.ok ? '' : (j.error || 'Błąd zapisu');
});

document.getElementById('btnLoadHistory').addEventListener('click', loadHistory);

refresh();
loadThermoConfig();
loadLogZoneConfig();
loadHistory();
setInterval(refresh, 5000);
</script>
</body>
</html>
)rawliteral";

const char* getDashboardHtml() {
    return DASHBOARD_HTML;
}

static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermo Control — Login</title>
<style>
  body { font-family: sans-serif; background:#1a1a2e; color:#e0e0e0; margin:0; padding:16px;
         display:flex; align-items:center; justify-content:center; min-height:100vh; }
  .card { background:#16213e; border-radius:8px; padding:24px; width:280px; }
  h1 { color:#4fc3f7; font-size:1.3em; margin-top:0; }
  input { width:100%; padding:8px; margin:8px 0; border-radius:4px; border:none; box-sizing:border-box; }
  button { width:100%; padding:8px; border-radius:4px; border:none; background:#4fc3f7; cursor:pointer; }
  #err { color:#ef5350; font-size:.9em; min-height:1.2em; }
</style>
</head>
<body>
<div class="card">
  <h1>Thermo Control</h1>
  <form id="f">
    <input type="password" id="password" placeholder="Hasło" autofocus>
    <button type="submit">Zaloguj</button>
    <div id="err"></div>
  </form>
</div>
<script>
document.getElementById('f').addEventListener('submit', async (e) => {
  e.preventDefault();
  const password = document.getElementById('password').value;
  const body = new URLSearchParams({password});
  const r = await fetch('/api/login', { method: 'POST', body });
  if (r.ok) { window.location.href = '/'; }
  else { document.getElementById('err').textContent = 'Błędne hasło'; }
});
</script>
</body>
</html>
)rawliteral";

const char* getLoginHtml() {
    return LOGIN_HTML;
}
