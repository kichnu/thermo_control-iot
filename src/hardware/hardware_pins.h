#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// ============================================================
// SEEED XIAO ESP32-C3 — Thermo Control Pin Mapping
// ============================================================
//
//                         ┌─────────────────────────┐
//                         │       [USB-C]            │
//                         │                          │
//               GND ──────┤ GND              GND ────┤
//                5V ──────┤ 5V              3V3 ─────┤
//               3V3 ──────┤ 3V3 (in)        RST ─────┤
//           NC (wolny) ───┤ D0/GPIO2   GPIO10/D10 ────┤ NC (wolny)
// SSR grzałka  ◄───────── ┤ D1/GPIO3    GPIO9/D9  ────┤ NC (wolny)
// Fan PWM      ◄───────── ┤ D2/GPIO4    GPIO8/D8  ────┤ NC (wolny)
// Energy pulse ─────────► ┤ D3/GPIO5   GPIO20/D7  ────┤ Provisioning btn
// I2C SDA      ◄─────────►┤ D4/GPIO6   GPIO21/D6  ────┤ Buzzer
// I2C SCL      ◄─────────►┤ D5/GPIO7              │
//                         └─────────────────────────┘
//
// I2C bus (GPIO6/7): DS3231 RTC (0x68) + MB85RC256V FRAM (0x50) + STS35 (0x4B)
//
// GPIO 18/19 = USB D+/D- — niedostępne
// GPIO 2/8/9/10 = wolne, zarezerwowane na przyszłość
// ============================================================

// ============== ACTUATORS ==============
#define HEATER_SSR_PIN      3   // SSR grzałki (active LOW: LOW = grzeje)
#define FAN_PWM_PIN         4   // Iduino ST1168 MOSFET PWM (HIGH = max prędkość)

// ============== I2C (DS3231 RTC + MB85RC256V FRAM + STS35 temp) ==============
#define I2C_SDA_PIN         6
#define I2C_SCL_PIN         7
#define STS35_I2C_ADDR      0x4B  // STS35 Sensirion (ADDR pin = VDD)

// ============== ENERGY METER ==============
#define ENERGY_PULSE_PIN    5   // Open-collector input, przez optocoupler PC817
                                // INPUT_PULLUP, ISR na FALLING

// ============== SYSTEM ==============
#define BUZZER_PIN         21   // Buzzer (HIGH = ON)
#define RESET_PIN          20   // Provisioning button (INPUT_PULLUP, active LOW, hold 5s)

// ============== WOLNE (rezerwa) ==============
// GPIO 2  (D0) — wolny
// GPIO 8  (D8) — wolny
// GPIO 9  (D9) — wolny
// GPIO 10 (D10) — wolny

#endif // HARDWARE_PINS_H
