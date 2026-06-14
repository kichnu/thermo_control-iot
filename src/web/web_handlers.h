#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <ESPAsyncWebServer.h>

// ============================================================
// API endpoints — Thermo Control
// ============================================================
// Auth (bez zmian z ATO):
//   POST /api/login, POST /api/logout, GET /api/health
//
// Thermo:
//   GET  /api/status
//   GET  /api/history
//   GET  /api/thermo-config
//   POST /api/thermo-config
//   POST /api/reset-alarm
//   POST /api/system-toggle
//   POST /api/system-reset
//   GET  /api/energy
//   POST /api/energy-reset-day
// ============================================================

void registerThermoHandlers(AsyncWebServer& server);

void handleStatus(AsyncWebServerRequest* request);
void handleHistory(AsyncWebServerRequest* request);
void handleGetThermoConfig(AsyncWebServerRequest* request);
void handleSetThermoConfig(AsyncWebServerRequest* request);
void handleResetAlarm(AsyncWebServerRequest* request);
void handleSystemToggle(AsyncWebServerRequest* request);
void handleSystemReset(AsyncWebServerRequest* request);
void handleEnergy(AsyncWebServerRequest* request);
void handleEnergyResetDay(AsyncWebServerRequest* request);

#endif // WEB_HANDLERS_H
