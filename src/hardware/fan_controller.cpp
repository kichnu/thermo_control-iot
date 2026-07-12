#include "fan_controller.h"
#include "hardware_pins.h"
#include "core/logging.h"

// TODO: Etap 2b — implementacja

#define FAN_LEDC_FREQ_HZ    25000
#define FAN_LEDC_RESOLUTION 8       // 8-bit: 0–255

static uint8_t s_fan_pct = 0;

void initFanController() {
    ledcAttach(FAN_PWM_PIN, FAN_LEDC_FREQ_HZ, FAN_LEDC_RESOLUTION);
    ledcWrite(FAN_PWM_PIN, 0);
    s_fan_pct = 0;
    LOG_INFO("FanController: LEDC PWM init, pin=%d freq=%dHz", FAN_PWM_PIN, FAN_LEDC_FREQ_HZ);
}

void setFanSpeed(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_fan_pct = pct;
    ledcWrite(FAN_PWM_PIN, (uint32_t)(pct * 255 / 100));
}

uint8_t getFanSpeedPct() { return s_fan_pct; }

uint8_t calcFanSpeed(float temp, float cool_start_abs, float cool_full_abs) {
    if (temp <= cool_start_abs) return 0;
    if (temp >= cool_full_abs)  return 100;
    float range = cool_full_abs - cool_start_abs;
    if (range < 0.01f) return 100;
    return (uint8_t)((temp - cool_start_abs) / range * 100.0f);
}

void fanEmergencyOff() { ledcWrite(FAN_PWM_PIN, 0); s_fan_pct = 0; }
