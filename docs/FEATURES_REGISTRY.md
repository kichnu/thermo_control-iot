# Rejestr funkcjonalności — Thermo Control

> Cel: wszystkie pomysły zebrane NA STARCIE. Wybierz co implementujesz.
> Po zatwierdzeniu: nie dodawaj nowych funkcji bez wpisu tutaj.

Status: [ ] = odrzucone, [v1] = pierwsza wersja, [v2] = przyszła wersja, [OUT] = poza scope

---

## BLOK A — Pomiar i czujniki

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| A-01 | STS35 temperatura wody (Sensirion I2C, ±0.1°C)      | [v1]   | Forbot STS35, adres 0x4B, offset w config|
| A-02 | Wejście impulsów miernika energii (cały system)     | [v1]   | GPIO interrupt, imp/kWh w GUI            |
| A-03 | Obliczona moc grzałki (heater_watt × duty_cycle)    | [v1]   | bez czujnika, heater_watt w config       |
| A-04 | Drugi STS35 (redundancja / góra vs dół zbiornika)   | [v2]   | alternatywny adres 0x4A (ADDR=GND)       |
| A-05 | Temperatura otoczenia (room temp)                   | [v2]   | DHT22 lub STS35 poza wodą                |
| A-06 | Fizyczny czujnik prądu AC (ACS712 lub SCT-013)      | [ ]    | ADC pin zarezerwowany, nie w v1          |
| A-07 | pH, TDS, przewodność                                | [OUT]  | osobny system, inne hardware             |

---

## BLOK B — Sterowanie aktuatorami

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| B-01 | Grzałka: SSR ON/OFF histereza                       | [v1]   | SSR-25DA, active LOW, termostat na 28°C  |
| B-02 | Wentylatory: MOSFET PWM 5–24V                       | [v1]   | LEDC ESP32, krzywa prędkości w config    |
| B-03 | Płynna zmiana temp przy przełączaniu trybu          | [v2]   | zapobiega stresowi ryb, Δ max 0.5°C/h   |
| B-04 | Manual override via web (wymuś heater ON/OFF)       | [v1]   | bezpiecznik czasowy (max 30 min)         |
| B-05 | Oświetlenie (PWM, harmonogram)                      | [OUT]  | osobny sterownik                         |
| B-06 | Dolewka wody (ATO)                                  | [OUT]  | osobny sterownik                         |

---

## BLOK C — Algorytm i tryby pracy

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| C-01 | ON/OFF histereza via SSR dla grzałki                | [v1]   | ±0.2°C histereza, konfigurowana          |
| C-02 | Proporcjonalny PWM wentylatorów (krzywa prędkości)  | [v1]   | liniowo od T_cool_start do T_cool_full   |
| C-03 | Tryb dzienny / nocny (inna T_set na noc)            | [v2]   | harmonogram HH:MM w GUI, RTC             |
| C-04 | Tryb urlopowy (alarmy push, lepsza oszczędność)     | [v2]   | via web toggle                           |
| C-05 | Profile temperaturowe (tarlisko, kuracja solna)     | [v2]   | maks. 3 profile, przełączanie w GUI      |
| C-06 | Detekcja szybkiego spadku temp (awaria grzałki)     | [v1]   | Δ > X°C / 10 min → alarm trend           |
| C-07 | Upgrade do PI time-proportioning (SSR)              | [ ]    | firmware only, bez zmian HW, gdy zajdzie |
| C-08 | Auto Mode / Service Mode (rename + UX)              | [v1]   | 2026-07: to samo co isSystemDisabled(), etykieta jak w dolewce |
| C-09 | Tryb warunkowego odchylenia temp. (sytuacje skrajne)| [ ]    | 2026-07: DO DYSKUSJI. User decyduje o wielkości odchyłki, ale GUI musi to jawnie "krzyczeć" — nie naginać ustawień po cichu. Niezaprojektowane. |

---

## BLOK D — Alarmy i powiadomienia

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| D-01 | Alarm temp LOW / HIGH (progi w config)              | [v1]   | buzzer FreeRTOS task; 2026-07: mute per-epizod (`/api/mute-alarm`) zamiast pełnego ack — flagi live, nie latchowane |
| D-02 | Alarm awaria czujnika (3× błąd CRC STS35)           | [v1]   | SSR OFF + fan OFF + buzzer               |
| D-03 | Alarm trend temp (Δ > próg / interwał)              | [v1]   | wczesne wykrycie awarii grzałki          |
| D-04 | Alarm przekroczenia energii dziennej                | [v2]   | suma kWh z miernika > limit/dzień        |
| D-05 | VPS logging / webhook (push alarmy)                 | [v2]   | wzorzec z projektu ATO, konfigurowany URL|
| D-06 | MQTT (HomeAssistant, Domoticz)                      | [v2]   | topic: thermo/status, thermo/alarm       |
| D-07 | Email powiadomienia                                 | [ ]    | wymaga SMTP, overkill dla ESP32          |

---

## BLOK E — Rejestracja i analiza danych

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| E-01 | Multi-res ring buffer w FRAM                        | [v1]   | patrz ARCHITECTURE_SKELETON §5           |
| E-02 | Heater duty cycle per rekord                        | [v1]   | heater_on_s lub heater_pct               |
| E-03 | Energia kWh z impulsów miernika (narastająco)       | [v1]   | uint32_t licznik impulsów w FRAM         |
| E-04 | Energia obliczona z SSR (heater_watt × duty)        | [v1]   | no hardware required                     |
| E-05 | Raport dzienny / tygodniowy energii via API         | [v1]   | obliczany z ring buffer                  |
| E-06 | Eksport CSV via /api/export                         | [v2]   | cały ring buffer jako CSV do pobrania    |
| E-07 | Threshold deadband compression (zapis gdy Δ > X)   | [v1]   | patrz ALGORITHM_DECISION §data           |
| E-08 | Detekcja anomalii (nagła zmiana → event record)     | [v2]   | znacznik w alarm_flags                   |

---

## BLOK F — Sieć i integracja

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| F-01 | WiFi + captive portal provisioning                  | [v1]   | z projektu ATO bez zmian                 |
| F-02 | REST API (auth cookie)                              | [v1]   | z projektu ATO, nowe endpointy           |
| F-03 | NTP sync przez RTC DS3231                           | [v1]   | z projektu ATO bez zmian                 |
| F-04 | MQTT broker client                                  | [v2]   | PubSubClient library                     |
| F-05 | OTA firmware update via web                         | [ ]    | ryzyko brick bez rollback, pominąć; nie mylić z dev-tooling ArduinoOTA (zaimplementowane, USB-only fallback) — patrz `docs/DEV_UPLOAD_LOGGING_CLI_ARCHITECTURE.md` §1 |

---

## BLOK G — UI i dashboard

| ID   | Funkcjonalność                                      | Status | Uwagi                                    |
|------|-----------------------------------------------------|--------|------------------------------------------|
| G-01 | Dashboard: aktualna temp + stan systemu             | [v1]   | mobile-friendly, responsive              |
| G-02 | Wykres temperatury (1h / 24h / 7d)                  | [v1]   | z ring buffer, Chart.js embedded         |
| G-03 | Wykres duty cycle / mocy grzałki                    | [v1]   | na tym samym wykresie co temp            |
| G-04 | Konfiguracja progów alarmowych                      | [v1]   | formularz web                            |
| G-05 | Konfiguracja harmonogramu dzień/noc                 | [v2]   | tylko gdy C-03 implemented               |
| G-06 | Podgląd licznika energii miernika                   | [v1]   | bieżące kWh + kWh/dzień                  |
| G-07 | Szacowany koszt miesięczny (PLN/kWh w config)       | [v2]   | taryfa w GUI                             |
| G-08 | Dark mode                                           | [ ]    | CSS prefers-color-scheme, niski priorytet|

---

## Decyzja: co wchodzi do v1

Lista v1 = minimum viable product:
A-01, A-02, A-03
B-01, B-02, B-04
C-01, C-02, C-06, C-08
D-01, D-02, D-03
E-01, E-02, E-03, E-04, E-05, E-07
F-01, F-02, F-03
G-01, G-02, G-03, G-04, G-06

Lista v2 = po walidacji v1 w terenie.
Lista [ ] = świadomie odrzucone, nie wracamy bez poważnego powodu.
