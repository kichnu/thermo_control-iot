#include "hw_health.h"
#include <esp_attr.h>
#include <esp_system.h>
#include "../core/logging.h"
#include "../config/credentials_manager.h"
#include "buzzer_controller.h"
#include "fram_controller.h"
#include "fram_constants.h"
#include "rtc_controller.h"

#define HW_CHECK_INTERVAL_MS     60000UL   // periodic check
#define HW_CHECK_SOON_MS          2000UL   // after a failed transfer
#define HW_CHECK_ATTEMPTS            3     // one failed transfer may be a glitch
#define HW_MAX_RECOVERY_RESTARTS     3     // per latch (until acknowledged) — no restart loop
#define HW_LATCH_MAGIC      0x48574C54UL   // "HWLT"

// Kept across software restarts, random after power-on (hence the magic)
struct HwLatch {
    uint32_t magic;
    uint8_t  mask;
    uint8_t  restarts;
    uint32_t sinceTs;
};
static RTC_NOINIT_ATTR HwLatch g_latch;

static volatile uint8_t g_active    = 0;
static volatile bool    g_checkSoon = false;
static uint32_t g_lastCheck       = 0;
static bool     g_recoveryWanted  = false;
static uint8_t  g_scratch         = 0;

static const uint8_t ALL_BITS[] = {HW_FAULT_FRAM, HW_FAULT_RTC, HW_FAULT_CRED};

// Magic read + write/read-back of a scratch byte. SPI has no ACK: a missing chip
// reads back 0x00/0xFF, so the magic and the scratch byte catch it.
static bool checkFram() {
    if (!isFramInitialized()) {
        // Missing at boot: RAM holds defaults that would overwrite the stored
        // data — probe only (FRAM stays unused) and restart once it answers
        if (framProbe()) g_recoveryWanted = true;
        return false;
    }
    for (int i = 0; i < HW_CHECK_ATTEMPTS; i++) {
        if (i) delay(20);
        uint32_t magic = 0;
        uint8_t  v = ++g_scratch, back = ~v;
        if (framReadRaw(FRAM_ADDR_MAGIC, (uint8_t*)&magic, 4) && magic == FRAM_MAGIC_NUMBER &&
            framWriteRaw(FRAM_ADDR_HEALTH_SCRATCH, &v, 1) &&
            framReadRaw(FRAM_ADDR_HEALTH_SCRATCH, &back, 1) && back == v) {
            return true;
        }
    }
    return false;
}

// DS3231 in use and giving a valid time (the ESP32 internal fallback is a fault).
// Not in use or not synced: retry it (back at once, or set from NTP)
static bool checkRtc() {
    retryRTC();
    if (!isRTCHardware()) return false;
    for (int i = 0; i < HW_CHECK_ATTEMPTS; i++) {
        if (i) delay(20);
        getUnixTimestamp();
        if (isUnixTimeValid()) return true;
    }
    return false;
}

static const char* bitName(uint8_t bit) {
    switch (bit) {
        case HW_FAULT_FRAM: return "FRAM";
        case HW_FAULT_RTC:  return "RTC";
        default:            return "Wi-Fi credentials";
    }
}

static void runCheck() {
    uint8_t active = 0;
    if (!checkFram()) active |= HW_FAULT_FRAM;
    if (!checkRtc())  active |= HW_FAULT_RTC;
    if (!areCredentialsLoaded()) active |= HW_FAULT_CRED;

    for (uint8_t bit : ALL_BITS) {
        if ((active & bit) && !(g_active & bit)) {
            LOG_ERROR("CRITICAL: %s fault — alarm latched%s", bitName(bit),
                      bit == HW_FAULT_RTC && isBatteryIssueDetected() ? " (DS3231 lost power — replace CR2032 battery)" : "");
        } else if (!(active & bit) && (g_active & bit)) {
            LOG_WARNING("%s works again — acknowledge in the GUI clears the alarm", bitName(bit));
        }
    }
    if (active & ~g_latch.mask) {
        if (!g_latch.mask) {
            uint32_t ts = (uint32_t)getUnixTimestamp();
            g_latch.sinceTs = isUnixTimeValid() ? ts : 0;
        }
        g_latch.mask |= active;
    }
    g_active = active;
}

void initHwHealth() {
    if (esp_reset_reason() == ESP_RST_POWERON || g_latch.magic != HW_LATCH_MAGIC) {
        g_latch = HwLatch{HW_LATCH_MAGIC, 0, 0, 0};
    } else if (g_latch.mask) {
        LOG_WARNING("Hardware alarm kept from before the restart (mask 0x%02X)", g_latch.mask);
    }
    runCheck();
    g_lastCheck = millis();
}

void updateHwHealth() {
    uint32_t now = millis();
    // Check soon only to catch a new fault — with one already active, failed
    // transfers come all the time and the retries would stall loop()
    bool soon = g_checkSoon && !g_active;
    if (now - g_lastCheck >= (soon ? HW_CHECK_SOON_MS : HW_CHECK_INTERVAL_MS)) {
        g_lastCheck = now;
        runCheck();
        g_checkSoon = false;   // after the check — its own failed transfers set it again
    }
    // The algorithm and the GUI switch the buzzer off (no alarm, mute, Service
    // Mode) — the latched critical alarm keeps it on until acknowledged
    if (g_latch.mask) setBuzzerMode(BUZZER_ALARM);
}

void noteFramIoError() { g_checkSoon = true; }
void noteRtcIoError()  { g_checkSoon = true; }

bool     isHwFaultAlarm()    { return g_latch.mask != 0; }
uint8_t  getHwFaultActive()  { return g_active; }
uint8_t  getHwFaultLatched() { return g_latch.mask; }
uint32_t getHwFaultSinceTs() { return g_latch.sinceTs; }

String getHwFaultText() {
    String s;
    for (uint8_t bit : ALL_BITS) {
        if (!(g_latch.mask & bit)) continue;
        if (s.length()) s += " · ";
        s += bitName(bit);
        // DS3231 kept no time over a power cut (OSF): the module works, the backup battery does not
        bool battery = bit == HW_FAULT_RTC && isBatteryIssueDetected();
        if (battery && !(g_active & bit)) s += " DS3231 lost power earlier, works now — replace CR2032 battery";
        else if (battery)               s += " DS3231 lost power, waiting for NTP — replace CR2032 battery";
        else if (!(g_active & bit))     s += " failed earlier, works now";
        else if (bit == HW_FAULT_FRAM)  s += " not responding or read/write check failed";
        else if (bit == HW_FAULT_RTC)   s += " DS3231 not responding or no valid time";
        else                            s += " not in FRAM — build-time fallback in use";
    }
    if (g_recoveryWanted) s += " (FRAM answers again — restarting)";
    return s;
}

bool ackHwFaults() {
    if (!g_latch.mask) return false;
    uint8_t keep = g_latch.mask & g_active;
    if (keep) {
        LOG_WARNING("Hardware alarm stays — still failing (mask 0x%02X)", keep);
    } else {
        LOG_INFO("Hardware alarm acknowledged");
        g_latch.sinceTs  = 0;
        g_latch.restarts = 0;
        setBuzzerMode(BUZZER_OFF);   // the algorithm sets it again within one cycle if its alarm is on
    }
    g_latch.mask = keep;
    return true;
}

bool isHwRecoveryRestartWanted() {
    return g_recoveryWanted && g_latch.restarts < HW_MAX_RECOVERY_RESTARTS;
}

void noteHwRecoveryRestart() {
    g_latch.restarts++;
    LOG_WARNING("FRAM missing at boot answers again — restart %u/%u",
                g_latch.restarts, HW_MAX_RECOVERY_RESTARTS);
}
