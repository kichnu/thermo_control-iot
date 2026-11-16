# Zgodność z regułami globalnymi

Reguły: `~/.claude/CLAUDE.md` („Sekrety w kodzie”, „Krytyczne bloki sprzętowe”). Wzorce i stan wszystkich
projektów: `~/dev-knowledge/iot-security/secrets-in-firmware.md`, `~/dev-knowledge/esp32/critical-hardware-alarm.md`.
Punkt naprawiony → oznaczyć tu i w tabeli w dev-knowledge. Audyt 2026-10-29; poprawki w kodzie 2026-11-15.

## Sekrety — ✅ kod czysty (2026-11-15)

- Było: prawdziwe SSID/hasło Wi-Fi w `src/config/config.cpp`, repo publiczne (kichnu/thermo_control-iot).
- Jest: `scripts/wifi_secret.py` (pre-script, kopia z lamp) → `IOT_WIFI_FALLBACK_*` z `~/.secrets/iot.env` do
  wygenerowanego `wifi_secret.h`; w repo żadnej wartości. Użycie fallbacku = alarm krytyczny (bit `CRED`).
- Zostaje: hasło w historii publicznego repo do rotacji (Krzysztof, stopniowo).

## Alarm krytyczny FRAM/RTC — ✅ w kodzie (2026-11-15), czeka na wgranie i test HW

- `src/hardware/hw_health.*` wg `top_off_water_new-iot`: FRAM (SPI) = magic + zapis/odczyt bajtu `0x1FFF`,
  RTC = DS3231 w użyciu z poprawnym rokiem, `CRED` = brak danych Wi-Fi w FRAM (fallback). Przy starcie, co 60 s
  i 2 s po nieudanym zapisie FRAM / odczycie RTC (3 próby).
- Zatrzask w `RTC_NOINIT_ATTR` (przeżywa restart nocny/OTA), buzzer `BUZZER_ALARM` ciągle — nie wycisza go
  „Mute Alarm” ani Service Mode, tylko „Acknowledge” w GUI, które kasuje wyłącznie to, co już działa.
- GUI: czerwony baner z przyczyną i przyciskiem; `/api/status`: `hw_alarm*`, `fram_ok`, `rtc_ok`, `cred_ok`;
  `/api/health`: `alarm` / `alarm_text` (też latch czujnika).
- Stan wykonawczy: regulacja temperatury działa dalej (potrzebuje tylko STS35; konfiguracja w RAM, przy braku FRAM
  przy starcie — domyślna). Świadomie bez wyłączania grzałki: awaria FRAM/RTC nie zagraża rybom, brak grzania tak.
- FRAM brak przy starcie → tylko sondowanie (FRAM nieużywany, RAM z domyślnymi nie nadpisze danych), restart po
  powrocie (maks. 3 na zatrzask). RTC: ponowna próba w trakcie pracy, ustawienie z NTP w tle.
- Poprawka przy okazji: przy utracie zasilania DS3231 nie zapisujemy już czasu kompilacji (kasował flagę OSF, więc
  kolejny start brał zły czas za poprawny) — czas ESP do ustawienia DS3231 z NTP.
- Bateria DS3231 (2026-11-16): reset zasilania z wyczerpaną CR2032 → flaga OSF → alarm RTC przy starcie (przed NTP),
  po ustawieniu z NTP „works now”. Tekst alarmu podaje wtedy przyczynę: „DS3231 lost power … replace CR2032 battery”
  (do następnego startu ze sprawną baterią). Sprawdzone na sprzęcie: zatrzask przy starcie i powrót po NTP.
- Do zrobienia: test na sprzęcie (odłączenie FRAM / RTC w trakcie pracy, start bez nich, potwierdzenie), dashboard
  VPS — pokazywanie pola `alarm`.
