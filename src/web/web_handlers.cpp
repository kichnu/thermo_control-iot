#include "web_handlers.h"
#include "security/auth_manager.h"
#include "security/session_manager.h"
#include "security/rate_limiter.h"
#include "algorithm/temp_algorithm.h"
#include "hardware/temp_sensor.h"
#include "hardware/heater_controller.h"
#include "hardware/fan_controller.h"
#include "hardware/energy_meter.h"
#include "hardware/fram_controller.h"
#include "hardware/buzzer_controller.h"
#include "config/config.h"
#include "web_server.h"
#include "core/logging.h"
#include "html_pages.h"
#include <ArduinoJson.h>

// TODO: Etap 5a — pełna implementacja

void registerThermoHandlers(AsyncWebServer& server) {
    server.on("/api/status",           HTTP_GET,  requireAuth(handleStatus));
    server.on("/api/history",          HTTP_GET,  requireAuth(handleHistory));
    server.on("/api/thermo-config",    HTTP_GET,  requireAuth(handleGetThermoConfig));
    server.on("/api/thermo-config",    HTTP_POST, requireAuth(handleSetThermoConfig));
    server.on("/api/log-zone-config",  HTTP_GET,  requireAuth(handleGetLogZoneConfig));
    server.on("/api/log-zone-config",  HTTP_POST, requireAuth(handleSetLogZoneConfig));
    server.on("/api/mute-alarm",       HTTP_POST, requireAuth(handleMuteAlarm));
    server.on("/api/system-toggle",    HTTP_POST, requireAuth(handleSystemToggle));
    server.on("/api/energy",           HTTP_GET,  requireAuth(handleEnergy));
    server.on("/api/energy-reset-day", HTTP_POST, requireAuth(handleEnergyResetDay));
    server.on("/api/test/fan",         HTTP_POST, requireAuth(handleTestFan));
    server.on("/api/test/heater",      HTTP_POST, requireAuth(handleTestHeater));
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
    // Limit odpowiedzi HTTP — bufor ma miejsce na 800 rekordów, ale nie serwujemy
    // wszystkich naraz. newest-first (patrz loadTempHistory()).
    const uint16_t MAX_RETURNED = 100;
    TempRecord buf[MAX_RETURNED];
    uint16_t n = loadTempHistory(buf, MAX_RETURNED);

    JsonDocument doc;
    JsonArray arr = doc["records"].to<JsonArray>();
    for (uint16_t i = 0; i < n; i++) {
        JsonObject r = arr.add<JsonObject>();
        r["ts"]        = buf[i].timestamp;
        r["temp_c"]    = buf[i].temp_x10 / 10.0f;
        r["zone"]      = (uint8_t)TREC_ZONE_GET(buf[i].zone_reason);
        r["reason"]    = (uint8_t)TREC_REASON_GET(buf[i].zone_reason);
        r["unstable"]  = (bool)TREC_UNSTABLE_GET(buf[i].zone_reason);
        r["heater_on"] = (bool)TREC_HEATER_ON_GET(buf[i].actuator_snapshot);
        r["fan_pct"]   = TREC_FAN_PCT_GET(buf[i].actuator_snapshot);
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleGetThermoConfig(AsyncWebServerRequest* request) {
    const ThermoConfig& cfg = thermoAlgorithm.getConfig();
    JsonDocument doc;
    doc["target_temp"]       = cfg.target_temp;
    doc["heat_hyst"]         = cfg.heat_hyst;
    doc["cool_start"]        = cfg.cool_start;
    doc["cool_full"]         = cfg.cool_full;
    doc["hysteresis_band"]   = cfg.hysteresis_band;
    doc["alarm_delta_low"]   = cfg.alarm_delta_low;
    doc["alarm_delta_high"]  = cfg.alarm_delta_high;
    doc["trend_alarm_delta"] = cfg.trend_alarm_delta;
    doc["temp_offset"]       = cfg.temp_offset;
    doc["heater_watt"]       = cfg.heater_watt;
    doc["pulses_per_kwh"]    = cfg.pulses_per_kwh;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static bool applyFloatParam(AsyncWebServerRequest* request, const char* name, float& out) {
    if (!request->hasParam(name, true)) return false;
    out = request->getParam(name, true)->value().toFloat();
    return true;
}

void handleSetThermoConfig(AsyncWebServerRequest* request) {
    ThermoConfig cfg = thermoAlgorithm.getConfig();

    applyFloatParam(request, "target_temp",       cfg.target_temp);
    applyFloatParam(request, "heat_hyst",         cfg.heat_hyst);
    applyFloatParam(request, "cool_start",        cfg.cool_start);
    applyFloatParam(request, "cool_full",         cfg.cool_full);
    applyFloatParam(request, "hysteresis_band",   cfg.hysteresis_band);
    applyFloatParam(request, "alarm_delta_low",   cfg.alarm_delta_low);
    applyFloatParam(request, "alarm_delta_high",  cfg.alarm_delta_high);
    applyFloatParam(request, "trend_alarm_delta", cfg.trend_alarm_delta);
    applyFloatParam(request, "temp_offset",       cfg.temp_offset);
    applyFloatParam(request, "heater_watt",       cfg.heater_watt);
    if (request->hasParam("pulses_per_kwh", true)) {
        cfg.pulses_per_kwh = (uint16_t)request->getParam("pulses_per_kwh", true)->value().toInt();
    }

    // Walidacja: progi muszą zachowywać sensowną kolejność fizyczną
    if (cfg.cool_full <= cfg.cool_start) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"cool_full must be > cool_start\"}");
        return;
    }
    if (cfg.alarm_delta_low <= cfg.heat_hyst || cfg.alarm_delta_high <= cfg.cool_start) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"alarm thresholds must exceed control thresholds\"}");
        return;
    }

    if (!thermoAlgorithm.setConfig(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"FRAM save failed\"}");
        return;
    }

    if (cfg.pulses_per_kwh > 0) setEnergyPulsesPerKwh(cfg.pulses_per_kwh);

    request->send(200, "application/json", "{\"ok\":true}");
}

void handleGetLogZoneConfig(AsyncWebServerRequest* request) {
    const LogZoneConfig& cfg = thermoAlgorithm.getLogZoneConfig();
    JsonDocument doc;
    doc["normal_delta"] = cfg.normal_delta;
    doc["low_step_1"]   = cfg.low_step_1;
    doc["low_step_2"]   = cfg.low_step_2;
    doc["high_step_1"]  = cfg.high_step_1;
    doc["high_step_2"]  = cfg.high_step_2;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleSetLogZoneConfig(AsyncWebServerRequest* request) {
    LogZoneConfig cfg = thermoAlgorithm.getLogZoneConfig();

    applyFloatParam(request, "normal_delta", cfg.normal_delta);
    applyFloatParam(request, "low_step_1",   cfg.low_step_1);
    applyFloatParam(request, "low_step_2",   cfg.low_step_2);
    applyFloatParam(request, "high_step_1",  cfg.high_step_1);
    applyFloatParam(request, "high_step_2",  cfg.high_step_2);

    // Walidacja monotoniczności (patrz algorithm_config.h / ARCHITECTURE_SKELETON.md)
    if (cfg.low_step_1 >= cfg.low_step_2 ||
        cfg.high_step_1 >= cfg.high_step_2 ||
        cfg.low_step_1 <= cfg.normal_delta || cfg.high_step_1 <= cfg.normal_delta) {
        request->send(400, "application/json",
            "{\"ok\":false,\"error\":\"steps must be monotonic and > normal_delta\"}");
        return;
    }

    if (!thermoAlgorithm.setLogZoneConfig(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"FRAM save failed\"}");
        return;
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

void handleMuteAlarm(AsyncWebServerRequest* request) {
    thermoAlgorithm.muteActiveAlarms();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSystemToggle(AsyncWebServerRequest* request) {
    bool wasDisabled = isSystemDisabled();
    setSystemState(wasDisabled);  // toggle
    if (isSystemDisabled()) {
        // Wejście w Service Mode — algorytm przestaje sterować buzzerem (patrz
        // TempAlgorithm::update()), więc trzeba go wyciszyć jawnie tutaj.
        // _alarm_flags NIE są czyszczone — status ostatniego alarmu zostaje
        // widoczny w GUI, cichnie tylko dźwięk.
        setBuzzerMode(BUZZER_OFF);
    }
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

// Test: ręczne sterowanie wentylatorem — bypass algorytmu (TempAlgorithm nadpisze
// przy kolejnym update(), chyba że system jest wstrzymany, patrz isSystemDisabled())
void handleTestFan(AsyncWebServerRequest* request) {
    if (!request->hasParam("pct", true)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing pct\"}");
        return;
    }
    int pct = request->getParam("pct", true)->value().toInt();
    if (pct < 0 || pct > 100) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"pct must be 0-100\"}");
        return;
    }
    setFanSpeed((uint8_t)pct);
    LOG_INFO("Test: fan set to %d%% via GUI", pct);
    request->send(200, "application/json", "{\"ok\":true}");
}

// Test: ręczne sterowanie grzałką — bypass algorytmu (TempAlgorithm nadpisze
// przy kolejnym update(), chyba że system jest wstrzymany, patrz isSystemDisabled())
void handleTestHeater(AsyncWebServerRequest* request) {
    if (!request->hasParam("on", true)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing on\"}");
        return;
    }
    bool on = request->getParam("on", true)->value().toInt() != 0;
    on ? setHeaterOn() : setHeaterOff();
    LOG_INFO("Test: heater set to %s via GUI", on ? "ON" : "OFF");
    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================
// Auth + strony statyczne (bez zmian z ATO)
// ============================================================

void handleDashboard(AsyncWebServerRequest* request) {
    request->send(200, "text/html", getDashboardHtml());
}

void handleLoginPage(AsyncWebServerRequest* request) {
    request->send(200, "text/html", getLoginHtml());
}

void handleLogin(AsyncWebServerRequest* request) {
    if (!request->hasParam("password", true)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"missing password\"}");
        return;
    }

    IPAddress clientIP = resolveClientIP(request);
    if (isRateLimited(clientIP)) {
        recordFailedAttempt(clientIP);
        request->send(429, "application/json", "{\"success\":false,\"error\":\"rate limited\"}");
        return;
    }

    String password = request->getParam("password", true)->value();
    if (!verifyPassword(password)) {
        recordFailedAttempt(clientIP);
        request->send(401, "application/json", "{\"success\":false,\"error\":\"invalid password\"}");
        return;
    }

    recordRequest(clientIP);
    String token = createSession(clientIP);

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"success\":true}");
    response->addHeader("Set-Cookie", "session_token=" + token + "; Path=/; HttpOnly");
    request->send(response);
}

void handleLogout(AsyncWebServerRequest* request) {
    if (request->hasHeader("Cookie")) {
        String cookie = request->getHeader("Cookie")->value();
        int tokenStart = cookie.indexOf("session_token=");
        if (tokenStart != -1) {
            tokenStart += 14;
            int tokenEnd = cookie.indexOf(";", tokenStart);
            if (tokenEnd == -1) tokenEnd = cookie.length();
            destroySession(cookie.substring(tokenStart, tokenEnd));
        }
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handleHealth(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}
