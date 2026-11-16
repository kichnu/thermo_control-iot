#pragma once
// ESP-NOW link to the touch panel (home_display-iot): status broadcast every STATUS_INTERVAL_MS and
// right after a relevant change (stage T1); commands TH_CMD → TH_ACK by unicast (stage T4), executed
// through core/node_commands like the web handlers. The receive callback only queues frames — HMAC,
// replay / nonce checks and the command itself run in loop().

#include <stdint.h>

void initEspNowPanel();     // after initWiFi(): needs the Wi-Fi password (key) and STA mode
void updateEspNowPanel();   // loop() context

struct EspNowPanelStats {
    bool     enabled;
    uint32_t txOk;
    uint32_t txFail;
    uint32_t rxCmd;        // commands executed (repeats of a cmd_id not counted)
    uint32_t rxBad;        // bad format / HMAC
    uint32_t rxReplay;     // old seq, or stale cmd_nonce
};
EspNowPanelStats getEspNowPanelStats();
