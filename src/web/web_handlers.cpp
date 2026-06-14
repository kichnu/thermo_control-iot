#include "web_handlers.h"
#include "security/auth_manager.h"
#include "security/session_manager.h"
#include "algorithm/temp_algorithm.h"
#include "hardware/temp_sensor.h"
#include "hardware/heater_controller.h"
#include "hardware/fan_controller.h"
#include "hardware/energy_meter.h"
#include "config/config.h"
#include "core/logging.h"
#include <ArduinoJson.h>

// TODO: Etap 5a — pełna implementacja

void registerThermoHandlers(AsyncWebServer& server) {
    server.on("/api/status",           HTTP_GET,  handleStatus);
    server.on("/api/history",          HTTP_GET,  handleHistory);
    server.on("/api/thermo-config",    HTTP_GET,  handleGetThermoConfig);
    server.on("/api/thermo-config",    HTTP_POST, handleSetThermoConfig);
    server.on("/api/reset-alarm",      HTTP_POST, handleResetAlarm);
    server.on("/api/system-toggle",    HTTP_POST, handleSystemToggle);
    server.on("/api/system-reset",     HTTP_POST, handleSystemReset);
    server.on("/api/energy",           HTTP_GET,  handleEnergy);
    server.on("/api/energy-reset-day", HTTP_POST, handleEnergyResetDay);
}

void handleStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["temp_c"]          = getTemperature();
    doc["state"]           = thermoAlgorithm.getStateString();
    doc["heater_on"]       = isHeaterOn();
    doc["fan_pct"]         = getFanSpeedPct();
    doc["alarm_flags"]     = thermoAlgorithm.getAlarmFlags();
    doc["system_disabled"] = isSystemDisabled();
    doc["energy_kwh_today"]= getEnergyKwhToday();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleHistory(AsyncWebServerRequest* request) {
    // TODO: Etap 5a — odczyt ring buffer z FRAM
    request->send(200, "application/json", "{\"records\":[]}");
}

void handleGetThermoConfig(AsyncWebServerRequest* request) {
    // TODO: Etap 5a
    request->send(200, "application/json", "{}");
}

void handleSetThermoConfig(AsyncWebServerRequest* request) {
    // TODO: Etap 5a — parsowanie form-encoded, walidacja, saveConfig()
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleResetAlarm(AsyncWebServerRequest* request) {
    thermoAlgorithm.acknowledgeAlarm();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSystemToggle(AsyncWebServerRequest* request) {
    setSystemState(isSystemDisabled());
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSystemReset(AsyncWebServerRequest* request) {
    thermoAlgorithm.resetStateMachine();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleEnergy(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["total_kwh"]   = getEnergyKwhTotal();
    doc["today_kwh"]   = getEnergyKwhToday();
    doc["pulse_count"] = getEnergyPulseCount();
    doc["power_w"]     = getPowerWatt();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleEnergyResetDay(AsyncWebServerRequest* request) {
    energyResetDayCounter();
    request->send(200, "application/json", "{\"ok\":true}");
}
