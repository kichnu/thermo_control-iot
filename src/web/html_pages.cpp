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
  .card { background:#16213e; border-radius:8px; padding:16px; margin:8px 0; }
  .temp { font-size:3em; color:#4fc3f7; }
  .badge { display:inline-block; padding:4px 10px; border-radius:12px; font-size:.85em; }
  .idle    { background:#2e7d32; }
  .heating { background:#e65100; }
  .cooling { background:#0277bd; }
  .alarm   { background:#b71c1c; }
</style>
</head>
<body>
<h1>Thermo Control</h1>
<div class="card">
  <div class="temp" id="temp">--.-°C</div>
  <span class="badge idle" id="state">IDLE</span>
  &nbsp;Grzałka: <span id="heater">OFF</span>
  &nbsp;Wentylator: <span id="fan">0%</span>
</div>
<div class="card">
  <b>Energia:</b> dziś: <span id="energy_today">0.000</span> kWh
  &nbsp;|&nbsp; moc: <span id="power">0</span> W
</div>
<div class="card">
  <b>Historia (ostatnie 50 dni) — TODO Chart.js</b><br>
  <small>Etap 5b</small>
</div>
<script>
async function refresh() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    document.getElementById('temp').textContent = d.temp_c.toFixed(1) + '°C';
    document.getElementById('state').textContent = d.state;
    document.getElementById('heater').textContent = d.heater_on ? 'ON' : 'OFF';
    document.getElementById('fan').textContent = d.fan_pct + '%';
    const e = await fetch('/api/energy');
    const en = await e.json();
    document.getElementById('energy_today').textContent = en.today_kwh.toFixed(3);
    document.getElementById('power').textContent = en.power_w.toFixed(0);
  } catch(err) { console.error(err); }
}
refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
)rawliteral";

const char* getDashboardHtml() {
    return DASHBOARD_HTML;
}
