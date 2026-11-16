#include "espnow_panel.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mbedtls/md.h>
#include <string.h>

#include "thermo_proto.h"
#include "../algorithm/temp_algorithm.h"
#include "../config/config.h"
#include "../config/credentials_manager.h"
#include "../core/logging.h"
#include "../core/node_commands.h"
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
static uint32_t s_rxCmd = 0;
static uint32_t s_rxBad = 0;
static uint32_t s_rxReplay = 0;

// Received frames: the Wi-Fi task only copies them here, loop() handles them
struct RxItem {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[FRAME_MAX];
};
static QueueHandle_t s_rxQueue = nullptr;
constexpr int RX_QUEUE_LEN = 4;

// Sender (panel) replay window and the last executed command, answered again on repeats
static uint8_t  s_peerMac[6];
static bool     s_havePeer = false;
static uint32_t s_peerBoot = 0;
static uint32_t s_peerSeq  = 0;
static bool     s_haveLastCmd = false;
static uint8_t  s_lastCmdMac[6];
static AckBody  s_lastAck;

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

static uint16_t mutedBits(uint16_t alarms) {
    uint8_t m = thermoAlgorithm.getMutedFlags();
    uint16_t b = 0;
    if (m & ALARM_FLAG_TEMP_LOW)  b |= AL_TEMP_LOW;
    if (m & ALARM_FLAG_TEMP_HIGH) b |= AL_TEMP_HIGH;
    return b & alarms;
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
    b.muted = mutedBits(a);
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

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    const uint8_t* mac = info->src_addr;
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
    if (!s_rxQueue || len <= 0 || len > (int)FRAME_MAX) return;
    RxItem it;
    memcpy(it.mac, mac, 6);
    it.len = (uint8_t)len;
    memcpy(it.data, data, len);
    xQueueSend(s_rxQueue, &it, 0);   // full queue: frame dropped, the panel repeats
}

static bool tagOk(const uint8_t* data, size_t len, const uint8_t* tag) {
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), data, len, full);
    uint8_t diff = 0;
    for (size_t i = 0; i < TAG_LEN; i++) diff |= full[i] ^ tag[i];
    return diff == 0;
}

static void ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.channel = 0;
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;
    esp_now_add_peer(&info);
}

static const char* cmdName(uint8_t c) {
    switch (c) {
        case CMD_SET_MODE:     return "SET_MODE";
        case CMD_ALARM_MUTE:   return "ALARM_MUTE";
        case CMD_SENSOR_RESET: return "SENSOR_RESET";
        case CMD_HEATER:       return "HEATER";
        case CMD_FAN:          return "FAN";
        default:               return "?";
    }
}

// Same functions as the web handlers (core/node_commands) — the rules live there
static AckResult execute(const CmdBody& c, uint16_t& value) {
    AckResult r;
    switch (c.cmd) {
        case CMD_SET_MODE: {
            if (c.arg > 1) return ACK_BAD_CMD;
            bool changed = false;
            r = cmdSetMode(c.arg == 1, &changed);
            if (r == ACK_OK && changed) LOG_INFO("System %s via panel", c.arg ? "ENABLED" : "DISABLED");
            break;
        }
        case CMD_ALARM_MUTE:   r = cmdAlarmMute(); break;
        case CMD_SENSOR_RESET: r = cmdSensorReset(); break;
        case CMD_HEATER:
            if (c.arg > 1) return ACK_BAD_CMD;
            r = cmdHeater(c.arg == 1);
            break;
        case CMD_FAN:          r = cmdFan(c.arg); break;
        default:               return ACK_BAD_CMD;
    }
    switch (c.cmd) {   // actual state after the command
        case CMD_SET_MODE: value = isSystemDisabled() ? 0 : 1; break;
        case CMD_HEATER:   value = isHeaterOn() ? 1 : 0; break;
        case CMD_FAN:      value = getFanSpeedPct(); break;
        default:           value = 0; break;
    }
    return r;
}

static void handleFrame(const RxItem& it) {
    if (it.len < sizeof(FrameHdr) + TAG_LEN) {
        s_rxBad++;
        return;
    }
    size_t n = it.len - TAG_LEN;
    FrameHdr h;
    memcpy(&h, it.data, sizeof(h));
    if (h.magic != FRAME_MAGIC || h.version != FRAME_VERSION) return;   // lamp / top-off frames — not ours
    if (!tagOk(it.data, n, it.data + n)) {
        s_rxBad++;
        return;
    }
    if (h.type != FT_CMD) return;   // only commands here
    if (n - sizeof(h) < sizeof(CmdBody)) {
        s_rxBad++;
        return;
    }
    bool samePeer = s_havePeer && memcmp(s_peerMac, it.mac, 6) == 0;
    if (samePeer && h.boot_id == s_peerBoot && h.seq <= s_peerSeq) {
        s_rxReplay++;
        return;
    }
    memcpy(s_peerMac, it.mac, 6);
    s_havePeer = true;
    s_peerBoot = h.boot_id;
    s_peerSeq  = h.seq;

    CmdBody c;
    memcpy(&c, it.data + sizeof(h), sizeof(c));
    ensurePeer(it.mac);

    // Repeat of the last command (ACK lost): the same answer, nothing executed again
    if (s_haveLastCmd && c.cmd_id == s_lastAck.cmd_id && memcmp(s_lastCmdMac, it.mac, 6) == 0) {
        sendFrame(it.mac, FT_ACK, &s_lastAck, sizeof(s_lastAck));
        return;
    }

    AckBody a = {};
    a.cmd_id = c.cmd_id;
    a.cmd    = c.cmd;
    if (c.cmd_nonce != s_cmdNonce) {
        s_rxReplay++;
        a.cmd_nonce = s_cmdNonce;
        a.result    = ACK_STALE_NONCE;
        a.alarms    = alarmBits();
        a.muted     = mutedBits(a.alarms);
        LOG_WARNING("Panel command %s rejected — stale nonce", cmdName(c.cmd));
        sendFrame(it.mac, FT_ACK, &a, sizeof(a));
        return;
    }

    uint16_t value = 0;
    AckResult r = execute(c, value);
    s_rxCmd++;
    s_cmdNonce = esp_random();
    a.cmd_nonce = s_cmdNonce;
    a.result    = r;
    a.value     = value;
    a.alarms    = alarmBits();
    a.muted     = mutedBits(a.alarms);
    s_lastAck = a;
    memcpy(s_lastCmdMac, it.mac, 6);
    s_haveLastCmd = true;
    LOG_INFO("Panel command %s arg %u: %s (value %u)", cmdName(c.cmd), c.arg, ackResultString(r), value);
    sendFrame(it.mac, FT_ACK, &a, sizeof(a));
    s_dueMs = millis() | 1u;   // status with the new state and nonce right away
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
    s_rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    esp_now_register_recv_cb(onRecv);

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
    RxItem it;
    while (s_rxQueue && xQueueReceive(s_rxQueue, &it, 0) == pdTRUE) handleFrame(it);
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
    return {s_enabled, s_txOk, s_txFail, s_rxCmd, s_rxBad, s_rxReplay};
}
