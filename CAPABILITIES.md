# MCS Control v1 — Hardware Capabilities

**Verified on prototype PCB: 2026-08-19**

## Architecture
- 2× galvanisk isolerede kanaler (A + B)
- Isolation via DC-DC (ROE-0505S) + optokoblere + separate I2C-expandere per kanal
- Kanal A og B kan køre helt forskellige spændinger og ground-referencer

## I/O Capabilities

| Feature | Kapacitet |
|---------|-----------|
| **Kanaler** | **2× galvanisk isolerede (A + B)** |
| Forsyning per kanal | Valgbar 5V / 12V / 24V (uafhængig, software-styret) |
| Relæ outputs | 2× mekanisk (1 per kanal, indbygget) |
| SSR outputs | Op til 8× ekstern SSR (via pullup 5kΩ, kræver 24V, ~5mA) |
| Analog input | 8× konfigurerbar: 4-20mA / 0-5V / 0-10V (ADS1115 16-bit) |
| Digital input | 8× opto-isoleret (virker ved 5V, 12V og 24V) |
| Identifikation | Auto HW-version + modul-nummer (hardwired på PCB, læst via I2C) |

## Communication

| Interface | Detaljer |
|-----------|----------|
| Ethernet | W5500 (SPI, on-board) |
| WiFi | ESP32-S3 intern (AP + STA) |
| BLE | ESP32-S3 intern |
| ESP-NOW | Peer-to-peer uden WiFi-infrastruktur |
| UART × 2 | RS-232 / RS-485 (software-skift via IO-expander) |
| USB Serial | Debug + konfiguration |

## User Interface

| Feature | Detaljer |
|---------|----------|
| Display | OLED 128×64, SPI (SSD1306) |
| Navigation | 5-knap analog resistor ladder |
| Menu-system | Config-drevet, animerede ikoner, smooth scroll |
| Custom screens | Relay override, voltage select, tank grafik |

## Hardware Platform

| Parameter | Værdi |
|-----------|-------|
| Processor | ESP32-S3 (dual-core 240MHz, FreeRTOS) |
| Flash | 16 MB |
| PSRAM | Ja |
| I2C devices | 4× TCA9535 (IO-expander) + 2× ADS1115 (ADC) |
| Board | Waveshare ESP32-S3-ETH |

## Input Modes (per kanal, software-konfigurerbart)

Hvert input har 4 CMOS-switches der kan kombineres:

| Switch | Funktion |
|--------|----------|
| SW_ANALOG | Forbinder til 10k/10k spændingsdeler → ADC |
| SW_PULLUP | 5kΩ til kanal-forsyning (5/12/24V) |
| SW_SHUNT | 200Ω til GND (strømmåling) |
| SW_DIGITAL | Opto-isoleret digital input til ESP32 GPIO |

**Modes:**
- **Voltage (0-5V/0-10V):** SW_ANALOG on → ADC læser via deler
- **4-20mA:** SW_ANALOG + SW_SHUNT on → strøm over 200Ω → ADC
- **Digital:** SW_DIGITAL on → opto-isoleret, inverteret logik
- **Digital + pullup:** SW_DIGITAL + SW_PULLUP on → self-powered sensor
- **SSR drive:** SW_PULLUP on (tænd) / SW_SHUNT on (sluk) → driver ekstern SSR

## DC-DC Outputs (RECOM ROE-0505S, ureguleret)

| Parameter | Værdi |
|-----------|-------|
| Max load | 85 mA per kanal |
| Ubelastet | ~6V / ~13.5V / ~26.5V (typisk) |
| Under last (50mA) | ~5.4V / ~12.7V / ~25.1V |
| Under last (100mA) | ~5.3V / ~12.0V / ~23.7V |
| Min. load anbefalet | ~10mA (LED) for stabil regulering → rev 2 |

## Self-test (kommando 'w')

88 automatiske tests uden ekstern tilslutning:
- Analog pullup alle 8 kanaler
- Analog pullup+shunt alle 8 kanaler
- Supply voltage måling ved 5V/12V/24V (ét input ad gangen)
- Digital HIGH/LOW ved 5V/12V/24V via opto

## Known Limitations (rev 1)
- SSR drive kræver 24V (12V giver kun ~2.5mA, ikke nok)
- ADC clamper ved >5.2V — pullup + analog ALDRIG ved 12V/24V
- DC-DC ureguleret — spænding varierer med last
- Mangler min. load LED/modstand på DC-DC (rev 2)
- UART + buzzer ikke loddet/testet endnu
