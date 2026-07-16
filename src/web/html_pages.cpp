#include "html_pages.h"

// TODO: Etap 5b — wykresy Chart.js zamiast tabel hourly/daily/alarm (poza
// zakresem rundy C-09/lock/modale/panel ustawień, 2026-07-15)

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermo Control</title>
<style>
  :root {
    --bg-primary:#0a0f1a; --bg-card:#111827; --bg-input:#1e293b; --border:#2d3a4f;
    --text-primary:#f1f5f9; --text-secondary:#94a3b8; --text-muted:#64748b;
    --accent-blue:#38bdf8; --accent-cyan:#22d3d5; --accent-green:#22c55e;
    --accent-red:#ef4444; --accent-orange:#f97316; --accent-yellow:#eab308;
    --radius:12px; --radius-sm:8px; --radius-lg:16px;
    --font-xs:.72rem; --font-sm:.8rem; --font-md:.9rem; --font-lg:1.05rem;
    --transition-fast:.15s ease;
    --shadow:0 4px 24px rgba(0,0,0,.4);
    --service-bg:rgba(249,115,22,0.10); --service-bg-hover:rgba(249,115,22,0.20);
    --service-border:rgba(249,115,22,0.35); --service-text:#f97316;
  }
  /* Bez tego padding/border liczy się PO podziale przestrzeni przez flex —
     .fan-stepper (0 własnego paddingu + 2px border) i przyciski (40px
     paddingu + 2px border) wychodziły na różne szerokości mimo flex:1 1 0. */
  *, *::before, *::after { box-sizing:border-box; }
  body { font-family: sans-serif; background:var(--bg-primary); color:var(--text-primary); margin:0; padding:0; }
  .container { max-width:800px; margin:0 auto; padding:16px; }
  h1 { color:var(--text-primary); margin:0; font-size:1.25rem; font-weight:700; letter-spacing:-0.02em; }
  h2 { color:var(--text-secondary); font-size:var(--font-md); font-weight:600; text-transform:uppercase; letter-spacing:.05em; margin:0 0 8px; }
  .card { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin:8px 0; box-shadow:var(--shadow); }
  /* flex+wrap: na wąskim ekranie .temp-deviation (display:contents, patrz niżej)
     rozpada się na osobne itemy tego samego rzędu — dev-arrow zostaje przy
     #temp, a duży dev-value + dev-meta zawijają się pod spód. */
  .temp { display:flex; align-items:center; flex-wrap:wrap; column-gap:14px; row-gap:2px; font-size:3em; color:var(--accent-blue); }
  #temp { transition:font-size .2s ease; }
  /* Aktywne odstępstwo: podstawowa nastawa kurczy się o 30% (.7em w kontekście
     3em .temp) i dostaje przekreślenie — nowa wartość (.dev-value)
     przejmuje jej PEŁNY, oryginalny rozmiar (1em = te same 3em co .temp). */
  body.deviation-active #temp { text-decoration:line-through; opacity:.55; font-size:.7em; }
  /* display:contents: element sam nie tworzy boxa (i tym samym nie jest już
     osobnym flex-itemem), ale jego dzieci (dev-arrow/dev-value/dev-meta)
     stają się bezpośrednimi itemami .temp — to one, nie on, reagują na wrap.
     JS przełącza display między 'contents' i 'none'. */
  .temp-deviation { display:none; font-size:1em; }
  .temp-deviation .dev-arrow { font-size:.7em; color:var(--accent-orange); font-weight:700; }
  .temp-deviation .dev-value { font-size:1.4em; color:var(--accent-orange); font-weight:700; line-height:1.1; }
  .temp-deviation .dev-meta { display:flex; flex-direction:column; justify-content:center; }
  .temp-deviation .dev-reason { font-size:.32em; color:var(--text-secondary); font-weight:600; line-height:1.3; min-height:1.3em; }
  .temp-deviation .dev-since { font-size:.32em; color:var(--text-muted); font-weight:400; line-height:1.3; min-height:1.3em; }

  /* Nagłówek karty (wzorzec z dolewki: ikona-badge + uppercase h2 + separator) */
  .logo { display:flex; align-items:center; gap:12px; }
  .logo-icon { width:40px; height:40px; background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); border-radius:var(--radius-sm); display:flex; align-items:center; justify-content:center; flex-shrink:0; }
  .logo-icon svg { width:24px; height:24px; fill:var(--bg-primary); }
  .card-header { display:flex; align-items:center; gap:10px; margin-bottom:16px; padding-bottom:12px; border-bottom:1px solid var(--border); }
  .card-header h2 { margin:0; }
  .card-header-icon { width:28px; height:28px; border-radius:6px; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
  .card-header-icon svg { width:16px; height:16px; }

  /* Pierwsza karta: System Status — wzorzec dla pozostałych kart */
  .status-main { display:flex; align-items:center; justify-content:space-between; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); padding:12px 16px; gap:12px; position:relative; }
  .status-main.status-ok { border-color:rgba(34,197,94,0.35); background:rgba(34,197,94,0.05); }
  .status-main.status-ok::after { content:''; position:absolute; top:8px; right:8px; width:6px; height:6px; background:var(--accent-green); border-radius:50%; animation:pulse 2s infinite; }
  .status-main.status-error { border-color:rgba(239,68,68,0.4); background:rgba(239,68,68,0.06); }
  .status-main.status-warn { border-color:rgba(234,179,8,0.4); background:rgba(234,179,8,0.06); }
  .status-main.status-disabled { border-color:rgba(148,163,184,0.35); background:rgba(148,163,184,0.05); }
  .status-main-body { flex:1; min-width:0; }
  .status-main-sub { display:flex; flex-wrap:wrap; align-items:center; gap:8px; margin-top:8px; font-size:var(--font-sm); color:var(--text-secondary); }
  .sub-on { color:var(--accent-green); } .sub-off { color:var(--text-muted); }
  .sub-warn { color:var(--accent-yellow); } .sub-danger { color:var(--accent-red); }
  .sub-sep { color:var(--border); }
  .status-main-wifi { display:flex; flex-direction:column; align-items:center; gap:2px; flex-shrink:0; }
  .card-header .status-main-wifi { margin-left:auto; }
  .wifi-label { font-size:.6rem; color:var(--text-muted); text-transform:uppercase; letter-spacing:.05em; }
  .wifi-dot { font-size:1.1rem; line-height:1; }
  .status-main-wifi.wifi-on .wifi-dot { color:var(--accent-green); }
  .status-main-wifi.wifi-off .wifi-dot { color:var(--text-muted); }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.5} }

  .btn-service { background:var(--service-bg); border-color:var(--service-border); color:var(--service-text); }
  .btn-service:hover { background:var(--service-bg-hover); border-color:var(--service-border); }
  .badge { display:inline-block; padding:4px 10px; border-radius:12px; font-size:.85em; font-weight:600; border:1px solid transparent; }
  .idle    { background:rgba(34,197,94,0.15);  color:var(--accent-green);  border-color:rgba(34,197,94,0.3); }
  .heating { background:rgba(234,179,8,0.15);  color:var(--accent-yellow); border-color:rgba(234,179,8,0.3); }
  .cooling { background:rgba(34,211,213,0.15); color:var(--accent-cyan);   border-color:rgba(34,211,213,0.3); }
  .alarm   { background:rgba(239,68,68,0.15);  color:var(--accent-red);    border-color:rgba(239,68,68,0.3); }
  /* Panel "Algorithm Settings" — wzorzec z dolewki (settings.png): przycisk
     ⚙ Settings poza kartą, cała karta domyślnie ukryta (display:none), grid
     dużych, wyśrodkowanych wartości monospace zamiast wąskich .row. */
  .settings-toggle-btn { margin:8px 0; font-size:var(--font-xs); padding:6px 10px; border-radius:6px; border:1px solid var(--border); background:var(--bg-input); color:var(--text-muted); cursor:pointer; }
  .settings-toggle-btn:hover { border-color:var(--accent-cyan); color:var(--text-primary); }
  .settings-title { font-size:var(--font-xs); font-weight:600; color:var(--text-muted); letter-spacing:.05em; text-transform:uppercase; margin-bottom:10px; }
  .settings-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(140px,1fr)); gap:8px 14px; }
  .settings-field { font-size:var(--font-xs); color:var(--text-muted); }
  .settings-field input { display:block; width:100%; margin-top:3px; padding:10px 14px; font-family:'Courier New',monospace; font-size:1rem; text-align:center; background:var(--bg-primary); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); }
  .settings-field input:focus { outline:none; border-color:var(--accent-cyan); box-shadow:0 0 0 3px rgba(34,211,213,0.2); }
  button { display:flex; align-items:center; justify-content:center; gap:8px; padding:10px 20px; border-radius:var(--radius-sm); border:1px solid var(--border); background:var(--bg-input); color:var(--text-primary); cursor:pointer; margin:4px 4px 4px 0; font-family:inherit; font-size:.9rem; font-weight:600; transition:all var(--transition-fast); }
  button:hover { border-color:var(--accent-cyan); }
  button:disabled { opacity:.45; cursor:not-allowed; }
  button:disabled:hover { border-color:var(--border); }
  button.primary { background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); border:none; color:var(--bg-primary); }
  button.danger { background:rgba(239,68,68,0.15); border-color:rgba(239,68,68,0.3); color:var(--accent-red); }

  /* Rząd przycisków wypełniający całą dostępną szerokość (System Control) */
  .btn-row { display:flex; flex-wrap:wrap; gap:8px; }
  .btn-row > button, .btn-row > .fan-stepper { flex:1 1 0; min-width:0; margin:0; padding-left:20px; padding-right:20px;}
  @media (max-width:600px) {
    .btn-row { flex-direction:column; }
    /* flex-basis:0 na osi pionowej (column) + overflow:hidden na .fan-stepper
       zerowały jego min-content wysokość (klasyczny problem flex
       min-height:auto+overflow) — w kolumnie liczy się naturalna wysokość,
       nie równy podział, więc flex-basis wraca do auto. */
    .btn-row > button, .btn-row > .fan-stepper { flex:0 0 auto; width:100%; }
  }
  table { border-collapse: collapse; width:100%; font-size:var(--font-sm); }
  th, td { text-align:left; padding:4px 8px; border-bottom:1px solid var(--border); color:var(--text-secondary); }
  #err_thermo, .err-text { color:var(--accent-red); font-size:var(--font-sm); }

  /* Topbar + lock */
  .topbar { display:flex; align-items:center; justify-content:space-between; margin-bottom:8px; }
  .topbar-actions { display:flex; align-items:center; gap:8px; }
  .btn-back { background:var(--bg-input); border:1px solid var(--border); color:var(--text-secondary); padding:8px 16px; border-radius:var(--radius-sm); font-size:.875rem; font-weight:500; cursor:pointer; transition:all .2s; }
  .btn-back:hover { background:var(--bg-card); color:var(--text-primary); }
  .lock-btn { display:inline-flex; align-items:center; gap:5px; background:none; border:1px solid var(--border); border-radius:var(--radius-sm); padding:8px 16px; font-size:var(--font-sm); font-weight:600; cursor:pointer; transition:color var(--transition-fast),border-color var(--transition-fast); }
  .lock-btn.locked { color:var(--text-muted); }
  .lock-btn.unlocked { color:var(--accent-green); border-color:var(--accent-green); }
  .lock-form-bar { display:none; gap:8px; align-items:center; background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius-sm); padding:8px 12px; margin-bottom:8px; }
  .lock-form-bar.visible { display:flex; }
  @keyframes lock-shake { 0%,100%{transform:translateX(0)} 25%{transform:translateX(-6px)} 75%{transform:translateX(6px)} }
  .shake-inp { animation:lock-shake .35s; }
  .lock-pwd-input { flex:1; height:34px; padding:0 10px; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); font-size:var(--font-md); outline:none; min-width:0; }
  .lock-pwd-input.error { border-color:var(--accent-red); }
  .lock-pwd-msg { font-size:var(--font-xs); color:var(--accent-red); min-width:100px; }
  .lock-close-btn { background:none; border:none; color:var(--text-muted); font-size:18px; cursor:pointer; padding:0 6px; }
  body.editing-locked .lockable-btn { pointer-events:none; opacity:.45; }
  body.editing-locked .lockable-btn::before { content:''; display:inline-block; width:11px; height:11px; vertical-align:-1px; margin-right:4px;
    background:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23f1f5f9' stroke-width='2.5'%3E%3Crect x='3' y='11' width='18' height='11' rx='2'/%3E%3Cpath d='M7 11V7a5 5 0 0 1 10 0v4'/%3E%3C/svg%3E") no-repeat center/contain; }

  /* Modal */
  .modal-overlay { position:fixed; top:0; left:0; right:0; bottom:0; background:rgba(0,0,0,.7); backdrop-filter:blur(4px); display:flex; align-items:center; justify-content:center; z-index:1000; opacity:0; visibility:hidden; transition:all .2s ease; }
  .modal-overlay.show { opacity:1; visibility:visible; }
  .modal-box { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius-lg); padding:24px; max-width:360px; width:90%; transform:scale(.9); transition:transform .2s ease; }
  .modal-overlay.show .modal-box { transform:scale(1); }
  .modal-icon { width:48px; height:48px; margin:0 auto 16px; border-radius:50%; display:flex; align-items:center; justify-content:center; }
  .modal-icon svg { width:24px; height:24px; }
  .modal-icon.ok   { background:rgba(34,197,94,.15); }  .modal-icon.ok svg   { color:var(--accent-green); }
  .modal-icon.err  { background:rgba(239,68,68,.15); }  .modal-icon.err svg  { color:var(--accent-red); }
  .modal-icon.warn { background:rgba(234,179,8,.15); }  .modal-icon.warn svg { color:var(--accent-yellow); }
  .modal-icon.info { background:rgba(56,189,248,.15); } .modal-icon.info svg { color:var(--accent-blue); }
  .modal-title { font-size:var(--font-lg); font-weight:700; text-align:center; margin-bottom:8px; }
  .modal-text { font-size:var(--font-sm); text-align:center; color:var(--text-secondary); margin-bottom:20px; line-height:1.5; }
  .modal-actions { display:flex; gap:10px; }
  .modal-actions button { flex:1; margin:0; }

  /* Odstępstwo od temp. zadanej (C-09) */
  .deviation-banner { background:rgba(249,115,22,0.12); border:1px solid rgba(249,115,22,0.35); border-radius:var(--radius-sm); padding:12px 14px; }
  .deviation-banner-title { color:var(--accent-orange); font-weight:700; font-size:var(--font-md); margin-bottom:4px; }

  /* Lista powodów odstępstwa (wzorzec notes-list) */
  .reason-section { background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); margin-top:10px; overflow:hidden; }
  .reason-header { display:flex; align-items:center; padding:8px 12px; gap:8px; cursor:pointer; }
  .reason-icon { flex-shrink:0; width:12px; height:12px; color:var(--accent-cyan); }
  #reasonHeaderLabel { flex:1; font-size:var(--font-lg); color:var(--text-secondary); font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .reason-header .reason-arrow { display:inline-block; transition:transform var(--transition-fast); }
  .reason-section.expanded .reason-header .reason-arrow { transform:rotate(180deg); }
  .reason-list { display:none; flex-direction:column; gap:6px; padding:8px 12px 12px; }
  .reason-section.expanded .reason-list { display:flex; }
  .reason-entry { display:flex; align-items:center; gap:6px; padding:2px; border-radius:var(--radius-sm); border:1px solid transparent; }
  .reason-entry span { width:18px; color:var(--text-muted); font-size:var(--font-xs); }
  .reason-entry input { flex:1; height:30px; padding:0 8px; background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); font-size:var(--font-sm); }
  .reason-entry.selected input { border-color:var(--accent-cyan); background:rgba(34,211,213,0.08); }
  /* Osobny przycisk wyboru (wzorzec note-sel-btn z dozownika) — klikalny
     obszar inputu wypełnia flex:1 całego wiersza, więc kliknięcie "w wiersz"
     prawie zawsze trafiało w input i było odrzucane jako edycja tekstu. */
  .reason-sel-btn { flex-shrink:0; width:18px; height:18px; border:none; background:none; color:var(--text-muted); cursor:pointer; display:flex; align-items:center; justify-content:center; border-radius:4px; transition:color var(--transition-fast); padding:0; }
  .reason-sel-btn:hover, .reason-sel-btn.active { color:var(--accent-cyan); }
  .reason-sel-btn svg { width:10px; height:10px; }
  .reason-sel-btn:disabled { opacity:.3; cursor:not-allowed; }

  /* Stepper prędkości wentylatora — wygląda jak jeden przycisk (kolory/wysokość
     spójne z resztą), podzielony na dwie strefy kliknięcia ze strzałkami */
  .fan-stepper { display:flex; align-items:stretch; border-radius:var(--radius-sm); border:1px solid var(--border); background:var(--bg-input); overflow:hidden; transition:all var(--transition-fast); }
  .fan-stepper.active { border-color:var(--service-border); background:var(--service-bg); }
  .fan-stepper.disabled { opacity:.45; pointer-events:none; }
  .fan-step-btn { background:none; border:none; color:var(--text-primary); padding:10px 14px; cursor:pointer; font-size:.9rem; margin:0; border-radius:0; }
  .fan-step-btn:hover { background:rgba(255,255,255,0.08); }
  .fan-step-btn:disabled { opacity:1; cursor:not-allowed; } /* fade już daje .fan-stepper.disabled na całości, bez podwójnego przyciemnienia */
  .fan-step-val { flex:1; padding:0 4px; font-size:.9rem; font-weight:600; color:var(--text-primary); min-width:46px; text-align:center; align-self:center; }
  .fan-stepper.active .fan-step-val, .fan-stepper.active .fan-step-btn { color:var(--service-text); }
</style>
</head>
<body>
<div class="container">

<div class="topbar">
  <div class="logo">
    <div class="logo-icon">
      <svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>
    </div>
    <h1>Thermo Control</h1>
  </div>
  <div class="topbar-actions">
    <button class="lock-btn locked" id="lockBtn" onclick="toggleLock()">LOCKED</button>
    <button class="btn-back" id="btnLogout">Back</button>
  </div>
</div>
<form id="lockFormBar" class="lock-form-bar" onsubmit="submitUnlock();return false;" autocomplete="off">
  <input type="password" id="lockPwdInput" class="lock-pwd-input" placeholder="PIN…" inputmode="numeric" maxlength="8" pattern="[0-9]*" autocomplete="one-time-code">
  <span id="lockFormMsg" class="lock-pwd-msg"></span>
  <button type="submit">Unlock</button>
  <button type="button" class="lock-close-btn" onclick="hideLockForm()">&#215;</button>
</form>

<!-- Pierwsza karta: SYSTEM STATUS — wzorzec dla pozostałych kart (2026-07-15) -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(56,189,248,0.15);">
      <svg fill="currentColor" style="color:var(--accent-blue);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
    </div>
    <h2>System Status</h2>
    <div class="status-main-wifi wifi-off" id="wifiItem">
      <span class="wifi-label">WiFi</span>
      <span class="wifi-dot" id="wifiStatus">●</span>
    </div>
  </div>
  <div class="status-main status-ok" id="statusMain">
    <div class="status-main-body">
      <div class="temp"><span id="temp">--.-°C</span><span class="temp-deviation" id="tempDeviation">
        <span class="dev-arrow" id="tempDeviationArrow">→</span>
        <span class="dev-value" id="tempDeviationValue"></span>
        <span class="dev-meta" id="tempDeviationMeta">
          <span class="dev-reason" id="tempDeviationReason"></span>
          <span class="dev-since" id="tempDeviationSince"></span>
        </span>
      </span></div>
      <div class="status-main-sub">
        <span class="badge idle" id="state">IDLE</span>
        <span class="sub-sep">•</span>
        <span>Heater: <span id="heater">OFF</span></span>
        <span class="sub-sep">•</span>
        <span>Fan: <span id="fan">0%</span></span>
        <span class="sub-sep">•</span>
        <span>Alarms: <span id="alarms">none</span></span>
      </div>
    </div>
  </div>
</div>

<div class="card">
  <b>Energia:</b> dziś: <span id="energy_today">0.000</span> kWh
  &nbsp;|&nbsp; moc: <span id="power">0</span> W
</div>

<!-- Druga karta: SYSTEM CONTROL — te same elementy graficzne co System Status.
     Ręczne sterowanie (heater/fan) i karta System/Service Mode jako całość
     to jedyny wyjątek od mechanizmu PIN-lock (potwierdzone przez użytkownika,
     2026-07-15) — dodatkowo ręczne przyciski są aktywne TYLKO w Service Mode
     (patrz .manual-ctrl w refresh()). -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(34,197,94,0.15);">
      <svg fill="currentColor" style="color:var(--accent-green);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 14.5v-9l6 4.5-6 4.5z"/></svg>
    </div>
    <h2>System Control</h2>
  </div>

  <div class="btn-row">
    <button id="btnSystemToggle">…</button>
    <button id="btnAck">Mute Alarm</button>
    <button id="btnResetSensor" class="danger">Reset Sensor Fault</button>
  </div>
  <div id="sensorFaultBanner" style="display:none; margin-top:8px; color:var(--accent-red); font-weight:bold;">
    ⚠ SENSOR FAULT (critical) — heater and fan frozen until manually confirmed
  </div>

  <div class="btn-row" style="margin-top:14px; padding-top:14px; border-top:1px solid var(--border);">
    <button class="manual-ctrl" id="heaterToggle">Heater: OFF</button>
    <button class="manual-ctrl" id="fanToggle">Fan: OFF</button>
    <div class="fan-stepper manual-ctrl disabled" id="fanStepper">
      <button type="button" class="fan-step-btn" onclick="stepFan(-1)">▼</button>
      <span class="fan-step-val" id="fanStepVal">0%</span>
      <button type="button" class="fan-step-btn" onclick="stepFan(1)">▲</button>
    </div>
  </div>
</div>

<!-- C-09: odstępstwo od temp. zadanej -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(249,115,22,0.15);">
      <svg fill="currentColor" style="color:var(--accent-orange);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
    </div>
    <h2>Temperature Deviation</h2>
  </div>

  <div class="btn-row">
    <button class="lockable-btn" id="deviationToggle">Deviation: OFF</button>
    <div class="fan-stepper disabled" id="devTempStepper">
      <button type="button" class="fan-step-btn" onclick="stepDevTemp(-1)">▼</button>
      <span class="fan-step-val" id="devTempVal">--.-°C</span>
      <button type="button" class="fan-step-btn" onclick="stepDevTemp(1)">▲</button>
    </div>
  </div>
  <div class="reason-section" id="reasonSection">
    <div class="reason-header" onclick="toggleReasonEditor()">
      <svg class="reason-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><polyline points="14,2 14,8 20,8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg>
      <span id="reasonHeaderLabel">Select reason…</span>
      <span class="reason-arrow">▾</span>
    </div>
    <div class="reason-list" id="reasonEditList"></div>
  </div>
</div>

<button id="algSettingsBtn" class="settings-toggle-btn" onclick="toggleAlgSettings()">⚙ Settings</button>

<div class="card" id="algSettingsPanel" style="display:none;">
  <div class="settings-title">Algorithm Parameters</div>
  <div class="settings-grid">
    <label class="settings-field">Target temperature [°C]<input type="number" step="0.1" id="target_temp"></label>
    <label class="settings-field">Heater hysteresis [°C]<input type="number" step="0.1" min="0.1" id="heat_hyst"></label>
    <label class="settings-field">Fan hysteresis [°C]<input type="number" step="0.1" min="0.1" id="cool_start"></label>
    <label class="settings-field">Fan 100% [°C]<input type="number" step="0.1" id="cool_full"></label>
    <label class="settings-field">Minimum fan speed [%]<input type="number" step="1" min="0" max="100" id="fan_min_pct"></label>
    <label class="settings-field">ALARM_LOW threshold [°C]<input type="number" step="0.1" id="alarm_delta_low"></label>
    <label class="settings-field">ALARM_HIGH threshold [°C]<input type="number" step="0.1" id="alarm_delta_high"></label>
    <label class="settings-field">Trend: required temp. [°C]<input type="number" step="0.1" id="trend_alarm_delta"></label>
    <label class="settings-field">Trend: window length [min]<input type="number" step="1" min="5" id="trend_window_min"></label>
    <label class="settings-field">Sensor calibration [°C]<input type="number" step="0.1" id="temp_offset"></label>
    <label class="settings-field">Heater power [W]<input type="number" step="1" id="heater_watt"></label>
    <label class="settings-field">Pulses / kWh (energy meter)<input type="number" step="1" id="pulses_per_kwh"></label>
  </div>
  <div id="err_thermo"></div>
  <div class="btn-row" style="margin-top:10px;">
    <button class="lockable-btn primary" id="btnSaveThermo">Save</button>
    <button type="button" onclick="toggleAlgSettings()">Cancel</button>
  </div>
</div>

<div class="card">
  <h2>Hourly History (rolling 24h, newest first)</h2>
  <button id="btnLoadHourly">Refresh</button>
  <table>
    <thead><tr><th>Hour</th><th>Avg. temp</th></tr></thead>
    <tbody id="hourlyBody"></tbody>
  </table>
</div>

<div class="card">
  <h2>Daily History (~1 year, newest first)</h2>
  <button id="btnLoadDaily">Refresh</button>
  <table>
    <thead><tr><th>Date</th><th>Avg. temp</th></tr></thead>
    <tbody id="dailyBody"></tbody>
  </table>
</div>

<div class="card">
  <h2>Alarm Events (occurrence log, newest first)</h2>
  <button id="btnLoadAlarms">Refresh</button>
  <div id="alarmSummary" style="margin:8px 0; font-size:.9em; color:var(--text-secondary);"></div>
  <table>
    <thead><tr><th>Time</th><th>Temp</th><th>Event</th></tr></thead>
    <tbody id="alarmEventsBody"></tbody>
  </table>
</div>

<div class="modal-overlay" id="alertModal">
  <div class="modal-box">
    <div class="modal-icon" id="alertIcon"></div>
    <div class="modal-title" id="alertTitle"></div>
    <div class="modal-text" id="alertText"></div>
    <div class="modal-actions" id="alertActions"></div>
  </div>
</div>

<script>
// AlarmEventType w algorithm_config.h — jeden wpis = fakt wystąpienia, bez start/end
const ALARM_EVENT_NAMES = ['LOW','HIGH','TREND','SENSOR'];

// Mirror FAN_MANUAL_DEFAULT_PCT z algorithm_config.h — brak mechanizmu
// wstrzykiwania #define do PROGMEM, wartość trzeba synchronizować ręcznie.
const FAN_MANUAL_DEFAULT_PCT = 50;
const FAN_STEP_PCT = 5;

// ── Modal boxes (zamiast alert()/confirm()) ─────────────────────────────
const MODAL_ICONS = {
  ok:   '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><polyline points="22,4 12,14.01 9,11.01"/></svg>',
  err:  '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>',
  warn: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
  info: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
};
let alertCallback = null;

function showAlert(title, msg, type) {
  document.getElementById('alertIcon').className = 'modal-icon ' + type;
  document.getElementById('alertIcon').innerHTML = MODAL_ICONS[type] || MODAL_ICONS.info;
  document.getElementById('alertTitle').textContent = title;
  document.getElementById('alertText').textContent = msg;
  alertCallback = null;
  document.getElementById('alertActions').innerHTML = '<button onclick="closeAlert()">OK</button>';
  document.getElementById('alertModal').classList.add('show');
}
function showConfirm(title, msg, type, onConfirm) {
  document.getElementById('alertIcon').className = 'modal-icon ' + type;
  document.getElementById('alertIcon').innerHTML = MODAL_ICONS[type] || MODAL_ICONS.info;
  document.getElementById('alertTitle').textContent = title;
  document.getElementById('alertText').textContent = msg;
  document.getElementById('alertActions').innerHTML =
    '<button onclick="closeAlert()">Cancel</button><button class="primary" onclick="closeAlert(true)">Confirm</button>';
  alertCallback = onConfirm;
  document.getElementById('alertModal').classList.add('show');
}
function closeAlert(confirmed) {
  document.getElementById('alertModal').classList.remove('show');
  if (confirmed && alertCallback) alertCallback();
  alertCallback = null;
}
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeAlert(); });
document.addEventListener('click', (e) => { if (e.target.id === 'alertModal') closeAlert(); });

// ── Lock edycji GUI (PIN, osobny od hasła logowania) ────────────────────
const LOCK_TIMEOUT_MS = 5 * 60 * 1000;
let isLocked = true, lockTimer = null, lockUnlockAt = 0;
let lockFailedAttempts = 0, lockThrottleUntil = 0, _lockCdInt = null;

function toggleLock() { if (isLocked) showLockForm(); else setLocked(true); }
function showLockForm() {
  document.getElementById('lockFormBar').classList.add('visible');
  const i = document.getElementById('lockPwdInput'); i.value = ''; i.focus();
  document.getElementById('lockFormMsg').textContent = '';
}
function hideLockForm() {
  document.getElementById('lockFormBar').classList.remove('visible');
  document.getElementById('lockPwdInput').value = '';
  document.getElementById('lockFormMsg').textContent = '';
}
function setLocked(locked) {
  isLocked = locked;
  if (locked) {
    clearTimeout(lockTimer); lockTimer = null;
    clearInterval(_lockCdInt); _lockCdInt = null;
    hideLockForm();
  } else {
    lockUnlockAt = Date.now() + LOCK_TIMEOUT_MS;
    resetLockTimer();
    startLockCountdown();
    hideLockForm();
  }
  updateLockUI();
}
function resetLockTimer() {
  if (isLocked) return;
  clearTimeout(lockTimer);
  lockUnlockAt = Date.now() + LOCK_TIMEOUT_MS;
  lockTimer = setTimeout(() => setLocked(true), LOCK_TIMEOUT_MS);
}
function lockMinsLeft() { return Math.max(1, Math.ceil((lockUnlockAt - Date.now()) / 60000)); }
function startLockCountdown() {
  clearInterval(_lockCdInt);
  _lockCdInt = setInterval(() => { if (isLocked) clearInterval(_lockCdInt); else updateLockUI(); }, 60000);
}
function updateLockUI() {
  const btn = document.getElementById('lockBtn');
  if (isLocked) { btn.className = 'lock-btn locked'; btn.textContent = 'LOCKED'; }
  else { btn.className = 'lock-btn unlocked'; btn.textContent = 'EDITING · ' + lockMinsLeft() + 'm'; }
  document.body.classList.toggle('editing-locked', isLocked);
}
function submitUnlock() {
  if (Date.now() < lockThrottleUntil) {
    document.getElementById('lockFormMsg').textContent = 'Wait ' + Math.ceil((lockThrottleUntil - Date.now()) / 1000) + 's…';
    return;
  }
  const inp = document.getElementById('lockPwdInput');
  const msg = document.getElementById('lockFormMsg');
  if (!inp.value) { inp.focus(); return; }
  fetch('api/verify-pin', { method: 'POST', body: new URLSearchParams({ pin: inp.value }) })
    .then(r => r.json()).then(d => {
      if (d.success) { lockFailedAttempts = 0; setLocked(false); }
      else {
        lockFailedAttempts++;
        inp.value = ''; inp.focus();
        inp.classList.add('error', 'shake-inp');
        setTimeout(() => inp.classList.remove('error', 'shake-inp'), 400);
        if (lockFailedAttempts >= 3) { lockThrottleUntil = Date.now() + 30000; lockFailedAttempts = 0; msg.textContent = 'Too many attempts. Wait 30s.'; }
        else msg.textContent = 'Wrong PIN (' + (3 - lockFailedAttempts) + ' left)';
      }
    }).catch(() => { msg.textContent = 'Error'; });
}

// Włącza/wyłącza stepper (div, nie <button> — .disabled CSS samo w sobie
// wystarczy przez pointer-events:none, ale disabled na wewnętrznych
// przyciskach robimy jawnie, żeby nie polegać wyłącznie na dziedziczeniu CSS).
function setStepperEnabled(el, enabled) {
  el.classList.toggle('disabled', !enabled);
  el.querySelectorAll('button').forEach(b => { b.disabled = !enabled; });
}

// ── Status / refresh ────────────────────────────────────────────────────
let currentFanPct = 0;
let currentHeaterOn = false;
let currentFanOn = false;
let serviceModeActive = false;
let deviationIsActive = false;
let devTargetTemp = 0;

async function refresh() {
  try {
    const r = await fetch('api/status');
    if (r.status === 401) { window.location.href = 'login'; return; }
    const d = await r.json();
    document.getElementById('temp').textContent = d.temp_c.toFixed(1) + '°C';
    document.getElementById('state').textContent = d.state;
    document.getElementById('heater').textContent = d.heater_on ? 'ON' : 'OFF';
    document.getElementById('fan').textContent = d.fan_pct + '%';
    document.getElementById('fanStepVal').textContent = d.fan_pct + '%';
    currentFanPct = d.fan_pct;

    // Przyciski bistabilne — label pokazuje stan BIEŻĄCY, klik wysyła przeciwny.
    currentHeaterOn = d.heater_on;
    currentFanOn = d.fan_pct > 0;
    document.getElementById('heaterToggle').textContent = 'Heater: ' + (currentHeaterOn ? 'ON' : 'OFF');
    document.getElementById('fanToggle').textContent = 'Fan: ' + (currentFanOn ? 'ON' : 'OFF');

    // Przycisk pokazuje stan BIEŻĄCY (nie cel kliknięcia) — Service Mode
    // dostaje ostrzegawczy pomarańcz (wzorzec .btn-service z dolewki).
    const sysBtn = document.getElementById('btnSystemToggle');
    sysBtn.textContent = d.system_disabled ? 'Service Mode' : 'Auto Mode';
    sysBtn.classList.toggle('btn-service', d.system_disabled);

    // Ręczne sterowanie (heater/fan) — aktywne TYLKO w Service Mode, z tym
    // samym pomarańczowym oznaczeniem tła co przycisk System (user, 2026-07-15).
    serviceModeActive = d.system_disabled;
    document.querySelectorAll('.manual-ctrl').forEach(el => {
      if (el.tagName === 'BUTTON') el.disabled = !serviceModeActive;
      el.classList.toggle('btn-service', serviceModeActive);
    });
    document.getElementById('fanStepper').classList.toggle('active', serviceModeActive);
    setStepperEnabled(document.getElementById('fanStepper'), serviceModeActive);

    const flags = [];
    if (d.alarm_flags & 0x01) flags.push('TEMP_LOW');
    if (d.alarm_flags & 0x02) flags.push('TEMP_HIGH');
    if (d.alarm_flags & 0x04) flags.push('TREND');
    if (d.alarm_flags & 0x08) flags.push('SENSOR');
    document.getElementById('alarms').textContent = flags.length ? flags.join(', ') : 'none';
    document.getElementById('sensorFaultBanner').style.display = d.sensor_fault_latched ? 'block' : 'none';

    const hasError = (d.alarm_flags !== 0) || d.sensor_fault_latched;
    const statusMain = document.getElementById('statusMain');
    statusMain.className = 'status-main';
    statusMain.classList.add(hasError ? 'status-error' : d.system_disabled ? 'status-disabled' : 'status-ok');

    document.getElementById('wifiItem').className = 'status-main-wifi ' + (d.wifi_connected ? 'wifi-on' : 'wifi-off');

    deviationIsActive = d.deviation_active;
    document.body.classList.toggle('deviation-active', deviationIsActive);

    // Przycisk bistabilny (jak Auto/Service Mode) — label + pomarańcz na czas
    // aktywnego odstępstwa. Zmiana wybranego powodu wymaga najpierw End —
    // stąd synchronizacja selectedReasonIdx z serwerem tylko gdy aktywne.
    const devBtn = document.getElementById('deviationToggle');
    devBtn.textContent = 'Deviation: ' + (deviationIsActive ? 'ON' : 'OFF');
    devBtn.classList.toggle('btn-service', deviationIsActive);
    if (deviationIsActive) selectedReasonIdx = d.deviation_reason_idx;
    if (deviationIsActive !== reasonUiLockedState) {
      reasonUiLockedState = deviationIsActive;
      renderReasonEditor();
      updateReasonHeaderLabel();
    }

    // Stepper nowej temp. zadanej — używalny zawsze gdy odstępstwo NIEaktywne
    // (żeby je zmienić trzeba najpierw End); niezależny od Service Mode —
    // to nie jest ręczne sterowanie sprzętem jak heater/fan (manual-ctrl).
    const devStepper = document.getElementById('devTempStepper');
    devStepper.classList.toggle('active', deviationIsActive);
    setStepperEnabled(devStepper, !deviationIsActive);

    // Nowa nastawa pojawia się przy głównej temp. (po prawej), podstawowa
    // nastawa dostaje przekreślenie (CSS: body.deviation-active #temp).
    const tempDev = document.getElementById('tempDeviation');
    if (deviationIsActive) {
      tempDev.style.display = 'contents';
      document.getElementById('tempDeviationValue').textContent =
        parseFloat(document.getElementById('target_temp').value).toFixed(1) + '°C';
      document.getElementById('tempDeviationReason').textContent =
        'Reason: ' + (deviationReasons[d.deviation_reason_idx] || '—');
      document.getElementById('tempDeviationSince').textContent =
        'active since ' + new Date(d.deviation_since * 1000).toLocaleString();
    } else {
      tempDev.style.display = 'none';
    }

    const e = await fetch('api/energy');
    const en = await e.json();
    document.getElementById('energy_today').textContent = en.today_kwh.toFixed(3);
    document.getElementById('power').textContent = en.power_w.toFixed(0);
  } catch(err) { console.error(err); }
}

async function loadThermoConfig() {
  const r = await fetch('api/thermo-config');
  const c = await r.json();
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','fan_min_pct',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','trend_window_min',
                    'temp_offset','heater_watt','pulses_per_kwh']) {
    if (c[k] !== undefined) document.getElementById(k).value = c[k];
  }
  // Stepper odstępstwa startuje zawsze od aktualnej efektywnej temp. zadanej
  // (normalnej albo już zdewiowanej) — po End automatycznie wraca do baseline.
  devTargetTemp = c.target_temp;
  document.getElementById('devTempVal').textContent = devTargetTemp.toFixed(1) + '°C';
}

function toggleAlgSettings() {
  const panel = document.getElementById('algSettingsPanel');
  panel.style.display = panel.style.display === 'none' ? '' : 'none';
}

// ── C-09: odstępstwo od temp. zadanej + lista powodów ───────────────────
let deviationReasons = [];
let selectedReasonIdx = null;
let reasonUiLockedState = null;

async function loadDeviationReasons() {
  const r = await fetch('api/deviation-reasons');
  const d = await r.json();
  deviationReasons = d.reasons || [];
  // Wzorzec z dozownika: nagłówek pokazuje konkretnie wybraną pozycję, nie
  // placeholder — domyślnie zaznacz pierwszy niepusty powód.
  if (selectedReasonIdx === null) {
    const firstIdx = deviationReasons.findIndex(t => t);
    if (firstIdx !== -1) selectedReasonIdx = firstIdx;
  }
  renderReasonEditor();
  updateReasonHeaderLabel();
}

// Wybór powodu = osobny przycisk (kółko, wzorzec note-sel-btn/selectNote z
// dozownika), nie kliknięcie wiersza — input zajmuje flex:1 wiersza, więc
// kliknięcie "w wiersz" prawie zawsze trafiało w pole tekstowe i było
// ignorowane. selectReason() aktualizuje DOM punktowo (bez przebudowy całej
// listy), tak jak selectNote() w dozowniku — pełny render tylko przy starcie
// i przy zmianie stanu blokady (start/koniec odstępstwa).
function renderReasonEditor() {
  const list = document.getElementById('reasonEditList');
  list.innerHTML = '';
  for (let i = 0; i < 8; i++) {
    const isSel = selectedReasonIdx === i;
    const row = document.createElement('div');
    row.id = 'reasonEntry_' + i;
    row.className = 'reason-entry' + (isSel ? ' selected' : '');
    const span = document.createElement('span'); span.textContent = i + 1;
    const inp = document.createElement('input');
    inp.type = 'text'; inp.maxLength = 29; inp.value = deviationReasons[i] || ''; inp.dataset.idx = i;
    inp.addEventListener('blur', () => {
      deviationReasons[i] = inp.value.slice(0, 29);
      saveDeviationReasons();
      document.getElementById('reasonSelBtn_' + i).disabled = deviationIsActive || !deviationReasons[i];
      if (selectedReasonIdx === i) updateReasonHeaderLabel();
    });
    const selBtn = document.createElement('button');
    selBtn.type = 'button';
    selBtn.id = 'reasonSelBtn_' + i;
    selBtn.className = 'reason-sel-btn' + (isSel ? ' active' : '');
    selBtn.disabled = deviationIsActive || !deviationReasons[i];
    selBtn.innerHTML = reasonSelSvg(isSel);
    selBtn.addEventListener('click', () => selectReason(i));
    row.appendChild(span); row.appendChild(inp); row.appendChild(selBtn);
    list.appendChild(row);
  }
}

function selectReason(i) {
  const prev = selectedReasonIdx;
  selectedReasonIdx = i;
  [prev, i].forEach((idx) => {
    if (idx === null || idx === undefined) return;
    const sel = idx === i;
    const entry = document.getElementById('reasonEntry_' + idx);
    const btn = document.getElementById('reasonSelBtn_' + idx);
    if (entry) entry.classList.toggle('selected', sel);
    if (btn) { btn.classList.toggle('active', sel); btn.innerHTML = reasonSelSvg(sel); }
  });
  updateReasonHeaderLabel();
}

function reasonSelSvg(filled) {
  return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="12" cy="12" r="4"' + (filled ? ' fill="currentColor"' : '') + '/></svg>';
}

function updateReasonHeaderLabel() {
  const text = selectedReasonIdx !== null ? deviationReasons[selectedReasonIdx] : null;
  document.getElementById('reasonHeaderLabel').textContent = text || 'Select reason…';
}

async function saveDeviationReasons() {
  const body = {};
  for (let i = 0; i < 8; i++) body['reason' + i] = deviationReasons[i] || '';
  await fetch('api/deviation-reasons', { method: 'POST', body: new URLSearchParams(body) });
}

function toggleReasonEditor() {
  document.getElementById('reasonSection').classList.toggle('expanded');
}

function stepDevTemp(dir) {
  if (document.getElementById('devTempStepper').classList.contains('disabled')) return;
  devTargetTemp = Math.round((devTargetTemp + dir * 0.5) * 10) / 10;
  document.getElementById('devTempVal').textContent = devTargetTemp.toFixed(1) + '°C';
}

function startDeviation() {
  if (selectedReasonIdx === null || !deviationReasons[selectedReasonIdx]) {
    // Świeże urządzenie ma 8 pustych slotów powodów — nie ma czego wybrać.
    // Zamiast cichego alertu, od razu rozwiń i podświetl listę.
    document.getElementById('reasonSection').classList.add('expanded');
    const firstInput = document.querySelector('#reasonEditList input');
    if (firstInput) firstInput.focus();
    showAlert('No reason selected', 'Add at least one reason in the list below, then select it by clicking its row.', 'err');
    return;
  }
  showConfirm('Start temperature deviation?', 'Target temperature will change to ' + devTargetTemp.toFixed(1) + '°C.', 'warn', async () => {
    const r = await fetch('api/deviation/start', { method: 'POST', body: new URLSearchParams({ target_temp: String(devTargetTemp), reason_idx: String(selectedReasonIdx) }) });
    const j = await r.json();
    if (!j.ok) { showAlert('Error', j.error || 'Failed to start deviation', 'err'); return; }
    refresh(); loadThermoConfig();
  });
}

async function endDeviation() {
  const r = await fetch('api/deviation/end', { method: 'POST' });
  const j = await r.json();
  if (!j.ok) { showAlert('Error', j.error || 'Failed to end deviation', 'err'); return; }
  refresh(); loadThermoConfig();
}

async function loadAvgHistory(endpoint, tbodyId) {
  const r = await fetch(endpoint);
  const d = await r.json();
  const tbody = document.getElementById(tbodyId);
  tbody.innerHTML = '';
  for (const rec of d.records) {
    const tr = document.createElement('tr');
    const time = new Date(rec.ts * 1000).toLocaleString();
    tr.innerHTML = `<td>${time}</td><td>${rec.temp_c.toFixed(1)}°C</td>`;
    tbody.appendChild(tr);
  }
}
const loadHourly = () => loadAvgHistory('api/history-hourly', 'hourlyBody');
const loadDaily  = () => loadAvgHistory('api/history-daily', 'dailyBody');

async function loadAlarmEvents() {
  const r = await fetch('api/alarm-events');
  const d = await r.json();

  // Skrót na górze: każdy wpis to fakt wystąpienia (bez start/end), więc liczba
  // zdarzeń danego typu = liczba wystąpień tego alarmu.
  const counts = {LOW:0, HIGH:0, TREND:0, SENSOR:0};
  for (const ev of d.events) counts[ALARM_EVENT_NAMES[ev.type]]++;
  const parts = ALARM_EVENT_NAMES.filter(c => counts[c] > 0).map(c => `${counts[c]}× ${c}`);
  document.getElementById('alarmSummary').textContent = d.events.length
    ? `${d.events.length} occurrences in returned history: ${parts.join(', ')}`
    : 'No alarm events in history.';

  const tbody = document.getElementById('alarmEventsBody');
  tbody.innerHTML = '';
  for (const ev of d.events) {
    const tr = document.createElement('tr');
    const time = new Date(ev.ts * 1000).toLocaleString();
    tr.innerHTML = `<td>${time}</td><td>${ev.temp_c.toFixed(1)}°C</td>` +
      `<td>${ALARM_EVENT_NAMES[ev.type] ?? ev.type}</td>`;
    tbody.appendChild(tr);
  }
}

document.getElementById('heaterToggle').addEventListener('click', async () => {
  await fetch('api/test/heater', { method: 'POST', body: new URLSearchParams({on: currentHeaterOn ? '0' : '1'}) });
  refresh();
});
document.getElementById('fanToggle').addEventListener('click', async () => {
  const pct = currentFanOn ? 0 : FAN_MANUAL_DEFAULT_PCT;
  await fetch('api/test/fan', { method: 'POST', body: new URLSearchParams({pct: String(pct)}) });
  refresh();
});
function stepFan(dir) {
  if (!serviceModeActive) return;
  let v = currentFanPct + dir * FAN_STEP_PCT;
  v = Math.max(0, Math.min(100, v));
  fetch('api/test/fan', { method: 'POST', body: new URLSearchParams({pct: String(v)}) }).then(refresh);
}

document.getElementById('deviationToggle').addEventListener('click', () => {
  if (deviationIsActive) endDeviation(); else startDeviation();
});

document.getElementById('btnSystemToggle').addEventListener('click', async () => {
  await fetch('api/system-toggle', { method: 'POST' });
  refresh();
});
document.getElementById('btnAck').addEventListener('click', async () => {
  await fetch('api/mute-alarm', { method: 'POST' });
  refresh();
});
document.getElementById('btnResetSensor').addEventListener('click', () => {
  showConfirm('Reset sensor fault?', 'Sensor fault is a critical alarm — confirm only if the fault has actually been fixed.', 'warn', async () => {
    await fetch('api/reset-sensor-fault', { method: 'POST' });
    refresh();
  });
});
document.getElementById('btnLogout').addEventListener('click', async () => {
  await fetch('api/logout', { method: 'POST' });
  window.location.href = 'login';
});

document.getElementById('btnSaveThermo').addEventListener('click', async () => {
  const body = {};
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','fan_min_pct',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','trend_window_min',
                    'temp_offset','heater_watt','pulses_per_kwh']) {
    body[k] = document.getElementById(k).value;
  }
  const r = await fetch('api/thermo-config', { method: 'POST', body: new URLSearchParams(body) });
  const j = await r.json();
  document.getElementById('err_thermo').textContent = j.ok ? '' : (j.error || 'Save failed');
});

document.getElementById('btnLoadHourly').addEventListener('click', loadHourly);
document.getElementById('btnLoadDaily').addEventListener('click', loadDaily);
document.getElementById('btnLoadAlarms').addEventListener('click', loadAlarmEvents);

updateLockUI();
loadDeviationReasons().then(refresh);
loadThermoConfig();
loadHourly();
loadDaily();
loadAlarmEvents();
setInterval(refresh, 5000);
</script>
</div>
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
  :root {
    --bg-primary:#0a0f1a; --bg-card:#111827; --border:#2d3a4f;
    --text-primary:#f1f5f9; --accent-blue:#38bdf8; --accent-cyan:#22d3d5; --accent-red:#ef4444;
    --radius:12px;
  }
  body { font-family: sans-serif; background:var(--bg-primary); color:var(--text-primary); margin:0; padding:16px;
         display:flex; align-items:center; justify-content:center; min-height:100vh; }
  .card { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius); padding:24px; width:280px; }
  h1 { color:var(--accent-blue); font-size:1.3em; margin-top:0; }
  input { width:100%; padding:8px; margin:8px 0; border-radius:8px; border:1px solid var(--border); background:#1e293b; color:var(--text-primary); box-sizing:border-box; }
  button { width:100%; padding:8px; border-radius:8px; border:none; background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); color:var(--bg-primary); font-weight:700; cursor:pointer; }
  #err { color:var(--accent-red); font-size:.9em; min-height:1.2em; }
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
  const r = await fetch('api/login', { method: 'POST', body });
  if (r.ok) { window.location.href = './'; }
  else { document.getElementById('err').textContent = 'Błędne hasło'; }
});
</script>
</body>
</html>
)rawliteral";

const char* getLoginHtml() {
    return LOGIN_HTML;
}
