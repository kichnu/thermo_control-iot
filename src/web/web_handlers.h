#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <ESPAsyncWebServer.h>

// ============================================================
// API endpoints — Thermo Control
// ============================================================
// Auth (unchanged from ATO):
//   POST /api/login, POST /api/logout, GET /api/health
//
// Thermo:
//   GET  /api/status
//   GET  /api/history-hourly
//   GET  /api/history-daily
//   GET  /api/alarm-events
//   GET  /api/thermo-config
//   POST /api/thermo-config
//   POST /api/mute-alarm
//   POST /api/reset-sensor-fault
//   POST /api/system-toggle       (enabled=0|1 sets the mode, without it toggles)
//   POST /api/test/heater (on), /api/test/fan (pct) — Service Mode only, rules in core/node_commands
//   GET  /api/energy
//   POST /api/energy-reset-day
//   POST /api/energy-reset-total  (kasowanie licznika total — gating PIN po stronie GUI, .lockable-btn)
//   POST /api/verify-pin          (pin) — GUI edit lock PIN, a second lock ON editing, not a login replacement
// ============================================================

void registerThermoHandlers(AsyncWebServer& server);

void handleStatus(AsyncWebServerRequest* request);
void handleHourlyHistory(AsyncWebServerRequest* request);
void handleDailyHistory(AsyncWebServerRequest* request);
void handleAlarmEvents(AsyncWebServerRequest* request);
void handleGetThermoConfig(AsyncWebServerRequest* request);
void handleSetThermoConfig(AsyncWebServerRequest* request);
void handleMuteAlarm(AsyncWebServerRequest* request);

// SENSOR_FAULT is a critical alarm (latched) — does not clear by itself, needs this call
void handleResetSensorFault(AsyncWebServerRequest* request);
void handleHwAlarmAck(AsyncWebServerRequest* request);
void handleSystemToggle(AsyncWebServerRequest* request);
void handleEnergy(AsyncWebServerRequest* request);
void handleEnergyResetDay(AsyncWebServerRequest* request);
void handleEnergyResetTotal(AsyncWebServerRequest* request);

// Manual fan (Service Mode only, core/node_commands) — POST pct=0-100
void handleTestFan(AsyncWebServerRequest* request);

// Manual heater (Service Mode only, ON refused with a sensor fault / above TEMP_HIGH) — POST on=0|1
void handleTestHeater(AsyncWebServerRequest* request);

// Test: fills / clears the hourly / daily / alarm buffers with random demo data — for GUI work
// before the device has collected a real history.
void handleTestSeedHistory(AsyncWebServerRequest* request);
void handleTestClearHistory(AsyncWebServerRequest* request);

// GUI edit lock PIN (separate from the login password) — a second lock on saving
void handleVerifyPin(AsyncWebServerRequest* request);

// ============================================================
// Auth + static pages (unchanged from ATO)
// ============================================================
void handleDashboard(AsyncWebServerRequest* request);
void handleLoginPage(AsyncWebServerRequest* request);
void handleLogin(AsyncWebServerRequest* request);
void handleLogout(AsyncWebServerRequest* request);
void handleHealth(AsyncWebServerRequest* request);

#endif // WEB_HANDLERS_H
