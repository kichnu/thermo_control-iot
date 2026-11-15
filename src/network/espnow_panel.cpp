#include "espnow_panel.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <string.h>

#include "thermo_proto.h"
#include "../algorithm/temp_algorithm.h"
#include "../config/config.h"
#include "../config/credentials_manager.h"
#include "../core/logging.h"
#include "../core/version.h"
#include "../hardware/fan_controller.h"
#include "../hardware/heater_controller.h"
#include "../hardware/hw_health.h"
#include "../hardware/rtc_controller.h"
#include "../hardware/temp_sensor.h"

using namespace thermo;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool     s_enabled = false;
static uint8_t  s_key[32];
static uint32_t s_bootId = 0;
static uint32_t s_txSeq = 0;
static uint32_t s_cmdNonce = 0;
static uint32_t s_txOk = 0;
static uint32_t s_txFail = 0;

static StatusBody s_lastSent;          // fields that trigger an immediate status (see changeKey())
static uint32_t   s_lastStatusMs = 0;
static uint32_t   s_dueMs = 0;         // != 0: change seen, send at this time

static void hmacTag(const uint8_t* data, size_t len, uint8_t* tag) {
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), data, len, full);
    memcpy(tag, full, TAG_LEN);
}

static bool sendFrame(const uint8_t* mac, FrameType type, const void* body, size_t bodyLen) {
    if (!s_enabled || WiFi.status() != WL_CONNECTED) return false;   // not connected = channel unknown
    uint8_t buf[FRAME_MAX];
    FrameHdr h = {FRAME_MAGIC, FRAME_VERSION, (uint8_t)type, s_bootId, ++s_txSeq};
    size_t n = sizeof(h) + bodyLen;
    if (n + TAG_LEN > sizeof(buf)) return false;
    memcpy(buf, &h, sizeof(h));
    if (bodyLen) memcpy(buf + sizeof(h), body, bodyLen);
    hmacTag(buf, n, buf + n);
    n += TAG_LEN;
    if (esp_now_send(mac, buf, n) != ESP_OK) {
        s_txFail++;
        return false;
    }
    s_txOk++;
    return true;
}

static uint16_t alarmBits() {
    uint8_t f = thermoAlgorithm.getAlarmFlags();
    uint16_t a = 0;
    if (f & ALARM_FLAG_TEMP_LOW)  a |= AL_TEMP_LOW;
    if (f & ALARM_FLAG_TEMP_HIGH) a |= AL_TEMP_HIGH;
    if (f & ALARM_FLAG_TREND)     a |= AL_TREND;
    if ((f & ALARM_FLAG_SENSOR) || thermoAlgorithm.isSensorFaultLatched()) a |= AL_SENSOR_FAULT;
    if (isHwFaultAlarm())         a |= AL_HW_FAULT;
    return a;
}

static uint8_t nodeStatus(uint16_t a) {
    if (a & (AL_SENSOR_FAULT | AL_HW_FAULT)) return NS_FAULT;
    if (a & (AL_TEMP_LOW | AL_TEMP_HIGH))    return NS_ALARM;
    if (isSystemDisabled())                  return NS_SERVICE;
    if (a)                                   return NS_WARNING;
    return NS_OK;
}

// withTime = false: change detection, skips the RTC read (I2C) — the time goes out with the frame only
static void buildStatus(StatusBody& b, bool withTime) {
    memset(&b, 0, sizeof(b));
    strncpy(b.name, getDeviceID(), sizeof(b.name) - 1);
    strncpy(b.fw, FW_VERSION, sizeof(b.fw) - 1);
    b.ip = (uint32_t)WiFi.localIP();
    bool timeValid = false;
    if (withTime) {
        uint32_t ts = (uint32_t)getUnixTimestamp();
        timeValid = isUnixTimeValid();
        b.utc = timeValid ? ts : 0;
    }
    b.cmd_nonce = s_cmdNonce;

    uint16_t a = alarmBits();
    b.alarms = a;
    uint8_t m = thermoAlgorithm.getMutedFlags();
    if (m & ALARM_FLAG_TEMP_LOW)  b.muted |= AL_TEMP_LOW;
    if (m & ALARM_FLAG_TEMP_HIGH) b.muted |= AL_TEMP_HIGH;
    b.muted &= a;
    b.node_status = nodeStatus(a);
    b.auto_mode = isSystemDisabled() ? 0 : 1;
    uint32_t left = getSystemAutoEnableRemainingS();
    b.service_left_s = left > 0xFFFF ? 0xFFFF : (uint16_t)left;
    b.algo_state = (uint8_t)thermoAlgorithm.getState();
    b.fan_pct = getFanSpeedPct();
    b.hw_fault = getHwFaultActive();

    float temp = getTemperature();
    bool tempValid = isTempValid();
    b.temp_x100 = tempValid ? (int16_t)lroundf(temp * 100.0f) : 0;

    // Same rule as /api/status in_buffer_zone (web_handlers.cpp)
    const ThermoConfig& cfg = thermoAlgorithm.getConfig();
    float fanOnPoint = cfg.target_temp + cfg.thermal_buffer;
    bool inBuffer = !isHeaterOn() && b.fan_pct == 0 && temp >= cfg.target_temp && temp < fanOnPoint;

    uint8_t f = 0;
    if (isHeaterOn())          f |= SF_HEATER_ON;
    if (b.fan_pct > 0)         f |= SF_FAN_ON;
    if (tempValid && inBuffer) f |= SF_IN_BUFFER;
    if (tempValid)             f |= SF_TEMP_VALID;
    if (timeValid)             f |= SF_TIME_VALID;
    b.flags = f;
}

// Fields that change all the time (clock, countdown, temperature) go out with the periodic
// status only; everything else sends the status right away.
static void changeKey(StatusBody& b) {
    b.utc = 0;
    b.flags &= ~SF_TIME_VALID;
    b.service_left_s = 0;
    b.temp_x100 = 0;
    b.ip = 0;
}

void initEspNowPanel() {
    const char* pass = getWiFiPassword();
    if (!pass || !pass[0]) {
        LOG_WARNING("ESP-NOW (panel) disabled — no Wi-Fi password (key)");
        return;
    }
    {   // key = SHA-256(context + Wi-Fi password)
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, (const uint8_t*)KEY_CONTEXT, strlen(KEY_CONTEXT));
        mbedtls_md_update(&ctx, (const uint8_t*)pass, strlen(pass));
        mbedtls_md_finish(&ctx, s_key);
        mbedtls_md_free(&ctx);
    }

    WiFi.setSleep(false);   // modem sleep drops ESP-NOW frames between beacons
    if (esp_now_init() != ESP_OK) {
        LOG_ERROR("ESP-NOW (panel): esp_now_init failed");
        return;
    }

    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, BROADCAST, 6);
    info.channel = 0;            // current channel (AP)
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;        // authenticity from HMAC; content is not secret
    esp_now_add_peer(&info);

    s_bootId = esp_random();
    s_cmdNonce = esp_random();
    memset(&s_lastSent, 0, sizeof(s_lastSent));
    s_enabled = true;
    LOG_INFO("ESP-NOW (panel) ready, MAC %s", WiFi.macAddress().c_str());
}

void updateEspNowPanel() {
    if (!s_enabled) return;
    uint32_t now = millis();

    StatusBody b;
    buildStatus(b, false);
    StatusBody key = b;
    changeKey(key);
    if (memcmp(&key, &s_lastSent, sizeof(key)) != 0 && !s_dueMs) {
        s_dueMs = now + STATUS_DEBOUNCE_MS;
        if (!s_dueMs) s_dueMs = 1;
    }

    bool due = (s_dueMs && (int32_t)(now - s_dueMs) >= 0) || now - s_lastStatusMs >= STATUS_INTERVAL_MS;
    if (!due) return;
    buildStatus(b, true);
    if (sendFrame(BROADCAST, FT_STATUS, &b, sizeof(b))) {
        s_lastSent = b;
        changeKey(s_lastSent);
    }
    s_lastStatusMs = now;
    s_dueMs = 0;
}

EspNowPanelStats getEspNowPanelStats() {
    return {s_enabled, s_txOk, s_txFail};
}
