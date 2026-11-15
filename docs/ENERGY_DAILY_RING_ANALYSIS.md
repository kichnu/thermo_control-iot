# Analiza: ring buffer dziennego zużycia energii

Status: **odłożone** (2026-08-17) — czeka na wymianę FRAM 8KB → 32KB.
Powiązane: `docs/FEATURES_REGISTRY.md` D-04 (alarm przekroczenia energii dziennej, [v2])
i E-05 (raport dzienny/tygodniowy energii, [v1] — dziś w praktyni niepełny, patrz niżej).

## Problem

Obecny licznik energii (`hardware/energy_meter.*`) daje tylko dwie liczby na żywo:
- `getEnergyKwhTotal()` — narastająco od resetu
- `getEnergyKwhToday()` — bieżąca doba (`total_pulses - day_start_pulses`)

`EnergyStore` (FRAM `0x0540`, 12B) trzyma `total_pulses` + `day_start_pulses` —
to jest odpowiednik "resetowanej co dobę zmiennej", tylko że przechowuje punkt
startowy doby, nie sam wynik. **Nie ma żadnej historii** — `energyResetDayCounter()`
(main.cpp:181, wołane tuż przed `ESP.restart()` o północy) zeruje `day_start` i
wcześniejsza wartość dnia znika bezpowrotnie. E-05 ("raport dzienny/tygodniowy
z ring buffera") w obecnym stanie nie ma z czego liczyć tygodnia.

## Miejsce przechwycenia wartości

`getEnergyKwhToday()` tuż przed wywołaniem `energyResetDayCounter()` w
main.cpp:181 (ten sam tick, przed `ESP.restart()`) — dokładnie tam trzeba
dopisać zapis do nowego ringu, zanim `day_start` zostanie przesunięty.

Timestamp początku kończącej się doby jest już policzony i dostępny w tym
samym zakresie main.cpp: `g_restartAtTs - 86400UL` (main.cpp:125,
`g_restartAtTs` = start *jutrzejszej* lokalnej północy) — nie trzeba
osobnego `localtime_r()`.

## Budżet FRAM (stan przed wymianą na 32KB)

Z `src/hardware/fram_constants.h`, wolne bloki:

| Zakres | Rozmiar | Uwaga |
|---|---|---|
| `0x055E–0x05FF` | 162B | rezerwa pod wzrost ThermoConfig/metadanych ringów — traktować jako zajęte |
| `0x1BA0–0x1CA1` | 258B | po LockPin, po usunięciu C-09 |
| `0x1CAA–0x1FFF` | 854B | koniec pamięci — jedyny naprawdę "wolny" ciągły blok |

Generyczne `saveRingRecord`/`loadRingRecords`/`getRingCount`
(`fram_controller.cpp:371-416`) są parametryzowane rozmiarem rekordu — nowy
ring to tylko nowy adres + nowy struct, bez zmian w silniku.

## Wariant A — własny timestamp (rekomendowany przy 8KB)

Struct identyczny wzorcem do `TempAvgRecord` (4B timestamp + 2B wartość,
padding do 8B przez wyrównanie `uint32_t`):

```cpp
struct EnergyDailyRecord {
    uint32_t timestamp;   // start zakończonej doby (lokalna północ, UTC) — g_restartAtTs - 86400
    uint16_t energy_wh;   // Wh (0–65535 ≈ 65 kWh/dobę) — bez utraty precyzji vs. licznik (1 imp = 1 Wh domyślnie)
};
static_assert(sizeof(EnergyDailyRecord) == 8, "...");
```

Adresacja (bez ruszania niczego istniejącego):
- metadane (count/wptr/checksum, 6B) → w luce `0x1BA0`
- dane → w ogonie `0x1CAA`, capacity = 854/8 = **106 rekordów ≈ 3,5 miesiąca** rolling

Zalety: zero couplingu z ringiem temperatury, kod = kopia
`saveDailyRecord`/`loadDailyHistory`, każdy rekord samodzielnie weryfikowalny
(własny timestamp → da się wykryć dziurę/przesunięcie).
Wada: krótka historia względem "roku" jak przy temp ringu.

## Wariant B — indeks współdzielony z dobowym ringiem temp (pełny rok)

Pomysł (Krzysztof, 2026-08-17): zamiast własnego timestampu, przechowywać
tylko wartość i czytać datę z rekordu o tym samym indeksie w istniejącym
dobowym ringu temperatury (`FRAM_ADDR_DAILY_BUFFER`, capacity 366).

```cpp
uint16_t energy_wh;  // albo nawet uint8_t w krokach 0.1 kWh, jeśli zależy na miejscu
```

Koszt: 366 × 2B = 732B (mieści się w ogonie 854B z zapasem) lub 366 × 1B = 366B.

**Wymaganie krytyczne**: zapis do obu ringów musi dziać się w dokładnie tym
samym wywołaniu/warunku, inaczej ich `wptr` się rozjadą i para
dzień↔energia zostanie cicho, trwale przesunięta bez możliwości wykrycia
(brak własnego timestampu = nie ma jak to zweryfikować później).

Konkretna przeszkoda: `_rollupDailyAverage()` (temp_algorithm.cpp:283) pomija
zapis dopóki bufor godzinowy nie ma pełnych 24 wpisów (pierwsza doba po
flashu/resecie) — a `energyResetDayCounter()` dziś jest wołane bezwarunkowo
co restart, niezależnie od tego guarda. Żeby indeksy się zgadzały, trzeba by
przenieść reset dobowego licznika energii do tego samego guarda co rollup
temperatury (nowa zależność modułu algorytmu od `energy_meter`, subtelna
zmiana zachowania "kWh dziś" przy pierwszym uruchomieniu).

To dokładnie ta klasa buga, którą projekt już raz złapał na żywym sprzęcie:
`checkLogRollover()` vs. restart race (main.cpp:154-164, naprawione
2026-08-15) — dwa niezależnie wyzwalane zapisy w okolicy północy, które
muszą trafić w ten sam takt, inaczej cichy błąd bez śladu.

## Sprzęt: FRAM 32KB to I2C, nie SPI

32KB FRAM w praktyce = I2C (np. Fujitsu MB85RC256V, adres domyślny 0x50,
zakres 0x50–0x57 via A0-A2) — SPI warianty tej pojemności nie są typowo
dostępne. ESP32-S3 ma tylko 2 sprzętowe kontrolery I2C i oba są już zajęte
(I2C0 = RTC, I2C1 = STS35, `hardware_pins.h`), ale to nie jest problem: I2C
to magistrala multi-drop, więc FRAM dopina się równolegle jako kolejne
urządzenie na istniejącej magistrali, bez nowych GPIO.

Rekomendacja: I2C0 (RTC, GPIO5/6, 400kHz, kable <5cm), nie I2C1 (STS35,
100kHz — spowolniony celowo przez ~1m kabla do czujnika). FRAM będzie mieć
znacznie częstszy ruch (ring buffery + config) niż lekki RTC; I2C1 ma
mniejszy margines na dodatkowe obciążenie.

Do sprawdzenia przy doborze modułu: adres 0x50 nie koliduje z 0x68 (RTC) ani
0x4A/0x4B (STS35, zresztą inna magistrala); pull-upy na I2C0 dobrane dziś
pod sam DS3231 — przy krótkich kablach dodanie drugiego urządzenia
praktycznie nieproblematyczne. Pin WP (write-protect) na module FRAM I2C
opcjonalny, można zostawić floating/GND jak dziś przy SPI FRAM (brak
sprzętowej ochrony zapisu).

Efekt uboczny: GPIO10-13 (dziś `FRAM_SPI_*`) całkowicie się zwalniają po
migracji.

## Rekomendacja

Wariant A przy obecnym 8KB FRAM — bezpieczniejszy, mniej nowego kodu ryzyka,
choć krótsza historia (~3,5 miesiąca).

Po wymianie FRAM na 32KB oba warianty przestają konkurować o miejsce — sensowne
jest wtedy wariant A ale z pełną capacity 366 (2928B, tyle co dziś zajmuje sam
ring temperatury), żeby zachować spójność wzorca i możliwość weryfikacji przez
niezależny timestamp bez kompromisu na długości historii.
