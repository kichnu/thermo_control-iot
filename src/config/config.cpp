#include "config.h"
#include "core/logging.h"
#include "hardware/fram_controller.h"
#include <Wire.h>
#include "hardware/hardware_pins.h"

// Placeholder credentials — zastępowane przez FRAM po provisioning
const char* WIFI_SSID          = "SETUP_REQUIRED";
const char* WIFI_PASSWORD      = "SETUP_REQUIRED";
const char* ADMIN_PASSWORD_HASH = nullptr;
const char* DEVICE_ID          = "UNCONFIGURED_DEVICE";
const IPAddress TRUSTED_PROXY_IP(10, 99, 0, 1);

// ============== SYSTEM DISABLE/ENABLE ==============

bool systemDisabled = true;  // boot w trybie serwisowym, auto-enable po SYSTEM_AUTO_ENABLE_MS
static unsigned long s_system_disabled_ms = 0;

void setSystemState(bool enabled) {
    systemDisabled = !enabled;
    s_system_disabled_ms = enabled ? 0 : millis();
    if (enabled) {
        LOG_INFO("=== SYSTEM ENABLED — algorytm wznawia pracę ===");
    } else {
        LOG_INFO("=== SYSTEM DISABLED — algorytm wstrzymany ===");
    }
}

bool isSystemDisabled() {
    return systemDisabled;
}

void checkSystemAutoEnable() {
    if (systemDisabled && s_system_disabled_ms > 0) {
        if (millis() - s_system_disabled_ms >= SYSTEM_AUTO_ENABLE_MS) {
            s_system_disabled_ms = 0;
            systemDisabled = false;
            LOG_INFO("=== SYSTEM AUTO-ENABLED (timeout) ===");
        }
    }
}

// ============== FRAM INIT ==============

void initSystemFRAM() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000);
    delay(50);
    if (!initFRAM()) {
        LOG_ERROR("FRAM initialization failed!");
    } else {
        LOG_INFO("FRAM ready (I2C 0x50)");
    }
    s_system_disabled_ms = millis();  // start auto-enable timer
}
