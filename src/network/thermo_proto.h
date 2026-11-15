#pragma once
// Wire format of the thermostat ESP-NOW protocol (node ↔ touch panel home_display-iot).
// Copied, not shared: the panel keeps the same file (src/thermo/thermo_proto.h) — keep both in sync.
// Plan: home_display-iot/docs/PLAN_TERMOSTAT.md.
//
// Frame = FrameHdr + body + HMAC-SHA256 truncated to 16 B over (hdr + body).
// Key = SHA-256(KEY_CONTEXT + Wi-Fi password) — own context, so lamp, top-off and thermostat frames
// never validate against each other.

#include <stddef.h>
#include <stdint.h>

namespace thermo {

constexpr uint16_t FRAME_MAGIC   = 0x4854;  // "TH" (bytes 54 48)
constexpr uint8_t  FRAME_VERSION = 1;
constexpr size_t   FRAME_MAX     = 250;     // ESP-NOW v1
constexpr size_t   TAG_LEN       = 16;
constexpr const char* KEY_CONTEXT = "THERMO-ESPNOW-v1:";

constexpr uint32_t STATUS_INTERVAL_MS = 5000;   // panel: 3 missed = offline
constexpr uint32_t STATUS_DEBOUNCE_MS = 300;    // change → status right away

enum FrameType : uint8_t {
    FT_STATUS = 1,   // node → broadcast
    FT_CMD    = 2,   // panel → node (stage T4)
    FT_ACK    = 3,   // node → panel (stage T4)
};

enum NodeStatus : uint8_t { NS_OK = 0, NS_WARNING, NS_ALARM, NS_FAULT, NS_SERVICE };

// Same values as the node's AlgorithmState
enum AlgoState : uint8_t {
    AS_IDLE = 0, AS_HEATING, AS_COOLING, AS_ALARM_LOW, AS_ALARM_HIGH, AS_SENSOR_FAULT, AS_TREND_ALARM,
};

// StatusBody.flags
constexpr uint8_t SF_HEATER_ON  = 0x01;
constexpr uint8_t SF_FAN_ON     = 0x02;
constexpr uint8_t SF_IN_BUFFER  = 0x04;  // temp >= target, fan not yet on (quiet note, yellow)
constexpr uint8_t SF_TEMP_VALID = 0x08;  // last sensor reading valid
constexpr uint8_t SF_TIME_VALID = 0x80;  // utc is a real time

// StatusBody.alarms / muted. Rule: latched alarms need intervention and cannot be muted;
// TEMP_LOW / TEMP_HIGH clear by themselves and can be muted for the current episode.
constexpr uint16_t AL_TEMP_LOW     = 0x0001;  // P2, buzzer, mutable
constexpr uint16_t AL_TEMP_HIGH    = 0x0002;  // P2, buzzer, mutable
constexpr uint16_t AL_TREND        = 0x0004;  // P3 warning, no buzzer (heater ineffective)
constexpr uint16_t AL_SENSOR_FAULT = 0x0008;  // P1, latched — heater and fan stopped, reset needed
constexpr uint16_t AL_HW_FAULT     = 0x0010;  // P1, latched — FRAM / RTC / Wi-Fi fallback, acknowledge needed

#pragma pack(push, 1)
struct FrameHdr {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint32_t boot_id;          // random at every boot
    uint32_t seq;              // increases within boot_id — replay rejection
};

struct StatusBody {
    char     name[32];         // device ID from provisioning
    char     fw[12];
    uint32_t ip;
    uint32_t utc;              // 0 = no valid time
    uint32_t cmd_nonce;        // commands must echo it (stage T4); changes after each executed command
    uint8_t  node_status;      // NodeStatus
    uint8_t  auto_mode;        // 1 = Auto, 0 = Service
    uint16_t service_left_s;   // Service Mode: seconds to the auto return (0 = none)
    uint8_t  algo_state;       // AlgoState
    uint8_t  flags;            // SF_*
    uint8_t  fan_pct;          // 0–100
    uint8_t  hw_fault;         // HW_FAULT_* bits failing now (FRAM 0x01, RTC 0x02, credentials 0x04)
    int16_t  temp_x100;        // °C × 100; valid with SF_TEMP_VALID
    uint16_t alarms;           // AL_*
    uint16_t muted;            // AL_* silenced by Mute (TEMP_LOW / TEMP_HIGH only)
};
#pragma pack(pop)

static_assert(sizeof(FrameHdr) == 12, "FrameHdr size");
static_assert(sizeof(StatusBody) == 70, "StatusBody size");
static_assert(sizeof(FrameHdr) + sizeof(StatusBody) + TAG_LEN <= FRAME_MAX, "status frame too long");

}  // namespace thermo
