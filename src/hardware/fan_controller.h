#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <Arduino.h>

// Iduino ST1168 MOSFET PWM — sterowanie wentylatorami DC 5–24V
// LEDC channel 0, 25kHz, 8-bit rozdzielczość (0–255)

void initFanController();

void setFanSpeed(uint8_t pct);  // 0 = stop, 100 = pełna prędkość
uint8_t getFanSpeedPct();

// Oblicz prędkość na podstawie temperatury i konfiguracji
// Zwraca 0–100 według krzywej liniowej [cool_start, cool_full]
uint8_t calcFanSpeed(float temp, float cool_start_abs, float cool_full_abs);

void fanEmergencyOff();

#endif // FAN_CONTROLLER_H
