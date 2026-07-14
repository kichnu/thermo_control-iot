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
#include "hardware/rtc_controller.h"
#include "config/config.h"
#include "web_server.h"
#include "core/logging.h"
#include "html_pages.h"
#include <ArduinoJson.h>

// TODO: Etap 5a — pełna implementacja

void registerThermoHandlers(AsyncWebServer& server) {
    server.on("/api/status",           HTTP_GET,  requireAuth(handleStatus));
    server.on("/api/history-hourly",   HTTP_GET,  requireAuth(handleHourlyHistory));
    server.on("/api/history-daily",    HTTP_GET,  requireAuth(handleDailyHistory));
    server.on("/api/alarm-events",     HTTP_GET,  requireAuth(handleAlarmEvents));
    server.on("/api/thermo-config",    HTTP_GET,  requireAuth(handleGetThermoConfig));
    server.on("/api/thermo-config",    HTTP_POST, requireAuth(handleSetThermoConfig));
    server.on("/api/mute-alarm",       HTTP_POST, requireAuth(handleMuteAlarm));
    server.on("/api/reset-sensor-fault", HTTP_POST, requireAuth(handleResetSensorFault));
    server.on("/api/system-toggle",    HTTP_POST, requireAuth(handleSystemToggle));
    server.on("/api/energy",           HTTP_GET,  requireAuth(handleEnergy));
    server.on("/api/energy-reset-day", HTTP_POST, requireAuth(handleEnergyResetDay));
    server.on("/api/test/fan",         HTTP_POST, requireAuth(handleTestFan));
    server.on("/api/test/heater",      HTTP_POST, requireAuth(handleTestHeater));
    server.on("/api/test/seed-history",  HTTP_POST, requireAuth(handleTestSeedHistory));
    server.on("/api/test/clear-history", HTTP_POST, requireAuth(handleTestClearHistory));
}

void handleStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["temp_c"]          = getTemperature();
    doc["state"]           = thermoAlgorithm.getStateString();
    doc["heater_on"]       = isHeaterOn();
    doc["fan_pct"]         = getFanSpeedPct();
    doc["alarm_flags"]     = thermoAlgorithm.getAlarmFlags();
    doc["sensor_fault_latched"] = thermoAlgorithm.isSensorFaultLatched();
    doc["system_disabled"] = isSystemDisabled();
    doc["energy_kwh_today"]= getEnergyKwhToday();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Bufory godzinowy/dobowy — patrz TempAvgRecord (algorithm_config.h). Obie
// pojemności (24 / 366) są już z natury małe, więc serwujemy komplet naraz.

void handleHourlyHistory(AsyncWebServerRequest* request) {
    TempAvgRecord buf[HOURLY_BUFFER_CAPACITY];
    uint16_t n = loadHourlyHistory(buf, HOURLY_BUFFER_CAPACITY);  // newest-first

    JsonDocument doc;
    JsonArray arr = doc["records"].to<JsonArray>();
    for (uint16_t i = 0; i < n; i++) {
        JsonObject r = arr.add<JsonObject>();
        r["ts"]     = buf[i].timestamp;
        r["temp_c"] = buf[i].temp_x10 / 10.0f;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleDailyHistory(AsyncWebServerRequest* request) {
    TempAvgRecord buf[DAILY_BUFFER_CAPACITY];
    uint16_t n = loadDailyHistory(buf, DAILY_BUFFER_CAPACITY);  // newest-first

    JsonDocument doc;
    JsonArray arr = doc["records"].to<JsonArray>();
    for (uint16_t i = 0; i < n; i++) {
        JsonObject r = arr.add<JsonObject>();
        r["ts"]     = buf[i].timestamp;
        r["temp_c"] = buf[i].temp_x10 / 10.0f;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleAlarmEvents(AsyncWebServerRequest* request) {
    // Limit odpowiedzi HTTP — bufor ma miejsce na ALARM_BUFFER_CAPACITY (300)
    // epizodów, ale nie serwujemy wszystkich naraz.
    const uint16_t MAX_RETURNED = 100;
    AlarmEvent buf[MAX_RETURNED];
    uint16_t n = loadAlarmEvents(buf, MAX_RETURNED);  // newest-first

    JsonDocument doc;
    JsonArray arr = doc["events"].to<JsonArray>();
    for (uint16_t i = 0; i < n; i++) {
        JsonObject r = arr.add<JsonObject>();
        r["ts"]     = buf[i].timestamp;
        r["temp_c"] = buf[i].temp_x10 / 10.0f;
        r["type"]   = buf[i].type;
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
    doc["fan_min_pct"]       = cfg.fan_min_pct;
    doc["alarm_delta_low"]   = cfg.alarm_delta_low;
    doc["alarm_delta_high"]  = cfg.alarm_delta_high;
    doc["trend_alarm_delta"] = cfg.trend_alarm_delta;
    doc["trend_window_min"]  = cfg.trend_window_min;
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
    applyFloatParam(request, "alarm_delta_low",   cfg.alarm_delta_low);
    applyFloatParam(request, "alarm_delta_high",  cfg.alarm_delta_high);
    applyFloatParam(request, "trend_alarm_delta", cfg.trend_alarm_delta);
    applyFloatParam(request, "temp_offset",       cfg.temp_offset);
    applyFloatParam(request, "heater_watt",       cfg.heater_watt);
    if (request->hasParam("pulses_per_kwh", true)) {
        cfg.pulses_per_kwh = (uint16_t)request->getParam("pulses_per_kwh", true)->value().toInt();
    }
    if (request->hasParam("trend_window_min", true)) {
        cfg.trend_window_min = (uint16_t)request->getParam("trend_window_min", true)->value().toInt();
    }
    if (request->hasParam("fan_min_pct", true)) {
        int v = request->getParam("fan_min_pct", true)->value().toInt();
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        cfg.fan_min_pct = (uint8_t)v;
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
    // heat_hyst/cool_start to teraz jedyny parametr histerezy na kanał (ON-offset,
    // OFF zawsze przy target_temp) — poniżej MIN_HYSTERESIS_OFFSET próg ON/OFF
    // tonie w szumie STS35 (±0.1°C) i grozi chatteringiem.
    if (cfg.heat_hyst < MIN_HYSTERESIS_OFFSET || cfg.cool_start < MIN_HYSTERESIS_OFFSET) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"heat_hyst/cool_start must be >= 0.1\"}");
        return;
    }
    if (cfg.trend_window_min < 5 || cfg.trend_alarm_delta <= 0.0f) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"trend_window_min must be >= 5, trend_alarm_delta must be > 0\"}");
        return;
    }

    if (!thermoAlgorithm.setConfig(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"FRAM save failed\"}");
        return;
    }

    if (cfg.pulses_per_kwh > 0) setEnergyPulsesPerKwh(cfg.pulses_per_kwh);

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

// Test: wypełnia bufory hourly/daily/alarm losowymi danymi do wglądu przy pracy
// nad GUI (wykresy, tabele) — zanim urządzenie zbierze prawdziwą historię.
// Zostawione w production build celowo (user, 2026-07): kod trywialny, endpoint
// wymaga auth jak reszta /api/test/*, więc nieszkodliwy poza ręcznym wywołaniem.
void handleTestSeedHistory(AsyncWebServerRequest* request) {
    float target = thermoAlgorithm.getConfig().target_temp;
    uint32_t now = (uint32_t)getUnixTimestamp();

    for (int i = HOURLY_BUFFER_CAPACITY - 1; i >= 0; i--) {
        TempAvgRecord rec;
        rec.timestamp = now - (uint32_t)i * 3600UL;
        rec.temp_x10  = (int16_t)((target + random(-30, 31) / 10.0f) * 10.0f);  // ±3.0°C
        saveHourlyRecord(rec);
    }

    for (int i = DAILY_BUFFER_CAPACITY - 1; i >= 0; i--) {
        TempAvgRecord rec;
        rec.timestamp = now - (uint32_t)i * 86400UL;
        rec.temp_x10  = (int16_t)((target + random(-20, 21) / 10.0f) * 10.0f);  // ±2.0°C
        saveDailyRecord(rec);
    }

    // 15 losowych faktów wystąpienia alarmu (bez start/end, patrz AlarmEvent),
    // rozrzuconych w ostatnich 90 dniach.
    const uint8_t types[4] = {ALARM_EVT_LOW, ALARM_EVT_HIGH, ALARM_EVT_TREND, ALARM_EVT_SENSOR};
    for (int i = 0; i < 15; i++) {
        AlarmEvent ev;
        ev.timestamp = now - (uint32_t)random(0, 90L * 86400L);
        ev.temp_x10  = (int16_t)((target + random(-30, 31) / 10.0f) * 10.0f);
        ev.type      = types[random(0, 4)];
        ev._pad      = 0;
        saveAlarmEvent(ev);
    }

    LOG_INFO("Test: seeded hourly/daily/alarm ring buffers with random demo data");
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleTestClearHistory(AsyncWebServerRequest* request) {
    clearHourlyHistory();
    clearDailyHistory();
    clearAlarmEvents();
    LOG_INFO("Test: cleared hourly/daily/alarm ring buffers");
    request->send(200, "application/json", "{\"ok\":true}");
}

// Ręczne potwierdzenie awarii czujnika (SENSOR_FAULT to od 2026-07-14 alarm
// krytyczny — latch, nie wraca sam nawet gdy czujnik znów daje poprawne
// odczyty). Jeśli usterka faktycznie wciąż trwa, kolejny update() i tak
// natychmiast zatrzaśnie ponownie.
void handleResetSensorFault(AsyncWebServerRequest* request) {
    thermoAlgorithm.resetSensorFaultLatch();
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
