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
#include "hardware/hw_health.h"
#include "core/node_commands.h"
#include "core/version.h"
#include "network/espnow_panel.h"
#include "network/wifi_manager.h"
#include "config/config.h"
#include "web_server.h"
#include "core/logging.h"
#include "html_pages.h"
#include <ArduinoJson.h>

// TODO: stage 5a — full implementation

void registerThermoHandlers(AsyncWebServer& server) {
    server.on("/api/status",           HTTP_GET,  requireAuth(handleStatus));
    server.on("/api/history-hourly",   HTTP_GET,  requireAuth(handleHourlyHistory));
    server.on("/api/history-daily",    HTTP_GET,  requireAuth(handleDailyHistory));
    server.on("/api/alarm-events",     HTTP_GET,  requireAuth(handleAlarmEvents));
    server.on("/api/thermo-config",    HTTP_GET,  requireAuth(handleGetThermoConfig));
    server.on("/api/thermo-config",    HTTP_POST, requireAuth(handleSetThermoConfig));
    server.on("/api/mute-alarm",       HTTP_POST, requireAuth(handleMuteAlarm));
    server.on("/api/reset-sensor-fault", HTTP_POST, requireAuth(handleResetSensorFault));
    server.on("/api/hw-alarm-ack",     HTTP_POST, requireAuth(handleHwAlarmAck));
    server.on("/api/system-toggle",    HTTP_POST, requireAuth(handleSystemToggle));
    server.on("/api/energy",           HTTP_GET,  requireAuth(handleEnergy));
    server.on("/api/energy-reset-day", HTTP_POST, requireAuth(handleEnergyResetDay));
    server.on("/api/energy-reset-total", HTTP_POST, requireAuth(handleEnergyResetTotal));
    server.on("/api/test/fan",         HTTP_POST, requireAuth(handleTestFan));
    server.on("/api/test/heater",      HTTP_POST, requireAuth(handleTestHeater));
    server.on("/api/test/seed-history",  HTTP_POST, requireAuth(handleTestSeedHistory));
    server.on("/api/test/clear-history", HTTP_POST, requireAuth(handleTestClearHistory));
    server.on("/api/verify-pin",        HTTP_POST, requireAuth(handleVerifyPin));
}

void handleStatus(AsyncWebServerRequest* request) {
    float temp = getTemperature();
    const ThermoConfig& cfg = thermoAlgorithm.getConfig();
    float fan_on_point = cfg.target_temp + cfg.thermal_buffer;
    // "Quiet note" in the GUI (see CLAUDE.md, thermal_buffer): temperature already above
    // target_temp but still below the fan ON point — the system waits, does nothing.
    bool in_buffer_zone = !isHeaterOn() && getFanSpeedPct() == 0 &&
                           temp >= cfg.target_temp && temp < fan_on_point;

    JsonDocument doc;
    doc["temp_c"]          = temp;
    doc["state"]           = thermoAlgorithm.getStateString();
    doc["heater_on"]       = isHeaterOn();
    doc["fan_pct"]         = getFanSpeedPct();
    doc["in_buffer_zone"]  = in_buffer_zone;
    doc["alarm_flags"]     = thermoAlgorithm.getAlarmFlags();
    doc["sensor_fault_latched"] = thermoAlgorithm.isSensorFaultLatched();
    doc["system_disabled"] = isSystemDisabled();
    doc["auto_enable_remaining_s"] = getSystemAutoEnableRemainingS();
    doc["energy_kwh_today"]= getEnergyKwhToday();
    doc["wifi_connected"]  = isWiFiConnected();
    // Critical hardware (hw_health): latched until it works again and is acknowledged
    doc["hw_alarm"]        = isHwFaultAlarm();
    doc["hw_alarm_text"]   = getHwFaultText();
    doc["hw_alarm_since"]  = getHwFaultSinceTs();   // unix, 0 = unknown
    doc["fram_ok"]         = !(getHwFaultActive() & HW_FAULT_FRAM);
    doc["rtc_ok"]          = !(getHwFaultActive() & HW_FAULT_RTC);
    doc["cred_ok"]         = !(getHwFaultActive() & HW_FAULT_CRED);
    doc["fw_version"]      = FW_VERSION;
    doc["energy_24h_kwh"]  = getEnergyKwh24h();   // last 24 full hours, changes once an hour
    {   // ESP-NOW link to the touch panel
        EspNowPanelStats es = getEspNowPanelStats();
        doc["espnow_enabled"] = es.enabled;
        doc["espnow_tx_ok"]   = es.txOk;
        doc["espnow_tx_fail"] = es.txFail;
        doc["espnow_rx_cmd"]    = es.rxCmd;
        doc["espnow_rx_bad"]    = es.rxBad;
        doc["espnow_rx_replay"] = es.rxReplay;
    }

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Hourly / daily buffers — see TempAvgRecord (algorithm_config.h). Both capacities (24 / 366) are
// small, so the whole set is served at once.

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
    // HTTP answer limit — the buffer holds ALARM_BUFFER_CAPACITY (300) episodes, but not all of
    // them are served at once.
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
    doc["thermal_buffer"]    = cfg.thermal_buffer;
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
    applyFloatParam(request, "thermal_buffer",    cfg.thermal_buffer);
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

    // Validation: the thresholds must keep a sensible physical order. cool_full and cool_start
    // count in OPPOSITE directions from fan_on_point (cool_start down to fan_off_point, cool_full
    // up to 100 %, see _updateFan()) — there is no direct cool_full > cool_start relation any
    // more, hence only a separate lower bound on cool_full below.
    if (cfg.cool_full <= 0.0f) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"cool_full must be > 0\"}");
        return;
    }
    if (cfg.alarm_delta_low <= cfg.heat_hyst) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"alarm_delta_low must exceed heat_hyst\"}");
        return;
    }
    // heat_hyst / cool_start are the only hysteresis parameter per channel (ON offset, OFF always
    // at target_temp) — below MIN_HYSTERESIS_OFFSET the ON/OFF threshold drowns in the STS35 noise
    // (±0.1 °C) and risks chattering.
    if (cfg.heat_hyst < MIN_HYSTERESIS_OFFSET || cfg.cool_start < MIN_HYSTERESIS_OFFSET) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"heat_hyst/cool_start must be >= 0.1\"}");
        return;
    }
    if (cfg.trend_window_min < 5 || cfg.trend_alarm_delta <= 0.0f) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"trend_window_min must be >= 5, trend_alarm_delta must be > 0\"}");
        return;
    }
    // thermal_buffer >= cool_start: otherwise fan_off_point (= fan_on_point - cool_start, see
    // _updateFan()) would fall below target_temp and the fan would fight the heater in its
    // normal range.
    if (cfg.thermal_buffer < cfg.cool_start) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"thermal_buffer must be >= cool_start\"}");
        return;
    }
    // cool_full < alarm_delta_high: both counted from the same fan_on_point (see
    // _updateFan() / _updateAlarms()) — otherwise ALARM_HIGH could fire before the fan reaches
    // 100 %, cutting the smooth ramp short.
    if (cfg.cool_full >= cfg.alarm_delta_high) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"cool_full must be < alarm_delta_high\"}");
        return;
    }

    if (!thermoAlgorithm.setConfig(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"FRAM save failed\"}");
        return;
    }

    if (cfg.pulses_per_kwh > 0) setEnergyPulsesPerKwh(cfg.pulses_per_kwh);

    request->send(200, "application/json", "{\"ok\":true}");
}

// Command result as HTTP: 200 {"ok":true} or 409 {"ok":false,"error":"..."} (rule of the node gate)
static void sendCmdResult(AsyncWebServerRequest* request, thermo::AckResult r) {
    if (r == thermo::ACK_OK) {
        request->send(200, "application/json", "{\"ok\":true}");
        return;
    }
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = ackResultString(r);
    String out;
    serializeJson(doc, out);
    request->send(r == thermo::ACK_OUT_OF_RANGE ? 400 : 409, "application/json", out);
}

void handleMuteAlarm(AsyncWebServerRequest* request) {
    sendCmdResult(request, cmdAlarmMute());
}

// enabled=1 / 0: set the mode (idempotent, as the panel); without it: toggle (web GUI button)
void handleSystemToggle(AsyncWebServerRequest* request) {
    bool autoMode = isSystemDisabled();
    if (request->hasParam("enabled", true)) autoMode = request->getParam("enabled", true)->value().toInt() != 0;
    bool changed = false;
    sendCmdResult(request, cmdSetMode(autoMode, &changed));
}

void handleEnergy(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["total_kwh"]   = getEnergyKwhTotal();
    doc["energy_24h_kwh"] = getEnergyKwh24h();
    doc["today_kwh"]   = getEnergyKwhToday();
    doc["pulse_count"] = getEnergyPulseCount();
    doc["power_w"]     = getPowerWatt();
    doc["reset_ts"]    = getEnergyResetTimestamp();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleEnergyResetDay(AsyncWebServerRequest* request) {
    energyResetDayCounter();
    request->send(200, "application/json", "{\"ok\":true}");
}

// Total counter reset — irreversible, so the GUI button has the .lockable-btn class (needs the PIN
// unlock first, the same mechanism as Save in Algorithm Settings, see gui_design_system_thermo).
void handleEnergyResetTotal(AsyncWebServerRequest* request) {
    energyResetTotalCounter();
    request->send(200, "application/json", "{\"ok\":true}");
}

// Manual fan (Service Mode only — checked by the node gate, not only by the GUI)
void handleTestFan(AsyncWebServerRequest* request) {
    if (!request->hasParam("pct", true)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing pct\"}");
        return;
    }
    sendCmdResult(request, cmdFan(request->getParam("pct", true)->value().toInt()));
}

// Manual heater (Service Mode only; ON refused with a sensor fault or above TEMP_HIGH — node gate)
void handleTestHeater(AsyncWebServerRequest* request) {
    if (!request->hasParam("on", true)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing on\"}");
        return;
    }
    sendCmdResult(request, cmdHeater(request->getParam("on", true)->value().toInt() != 0));
}

// Test: fills the hourly / daily / alarm buffers with random data for GUI work (charts, tables)
// before the device collects a real history. Kept in the production build on purpose (user,
// 2026-07): trivial code, the endpoint needs auth like the rest of /api/test/*, harmless unless
// called by hand.
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

    // 15 random alarm occurrences (no start/end, see AlarmEvent),
    // spread over the last 90 days.
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

// Acknowledge the latched hardware alarm: clears only the blocks that work again
void handleHwAlarmAck(AsyncWebServerRequest* request) {
    ackHwFaults();
    JsonDocument doc;
    doc["ok"]            = true;
    doc["hw_alarm"]      = isHwFaultAlarm();
    doc["hw_alarm_text"] = getHwFaultText();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Manual reset of the sensor fault (SENSOR_FAULT has been a critical alarm since 2026-07-14 —
// latched, does not clear by itself even when the sensor reads fine again). If the fault still
// lasts, the next update() latches it again at once.
void handleResetSensorFault(AsyncWebServerRequest* request) {
    sendCmdResult(request, cmdSensorReset());
}

// GUI edit lock PIN — separate from the login password (a second lock ON editing, needs an
// existing session, not a login replacement).
void handleVerifyPin(AsyncWebServerRequest* request) {
    if (!request->hasParam("pin", true)) {
        request->send(400, "application/json", "{\"success\":false}");
        return;
    }
    String pin = request->getParam("pin", true)->value();

    LockPin lp;
    loadLockPinFromFRAM(lp);
    bool match = (strncmp(pin.c_str(), lp.pin, sizeof(lp.pin)) == 0);
    request->send(200, "application/json", match ? "{\"success\":true}" : "{\"success\":false}");
}

// ============================================================
// Auth + static pages (unchanged from ATO)
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

// "status" stays "ok" (reachability contract of the VPS dashboard); "alarm" = a
// critical alarm (latched hardware fault, latched sensor fault)
void handleHealth(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["status"] = "ok";
    String text;
    if (isHwFaultAlarm()) text = getHwFaultText();
    if (thermoAlgorithm.isSensorFaultLatched()) {
        if (text.length()) text += " | ";
        text += "STS35 sensor fault — heater and fan stopped";
    }
    doc["alarm"] = text.length() > 0;
    if (text.length()) doc["alarm_text"] = text;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}
