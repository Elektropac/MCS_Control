# MCS Control Firmware

Firmware til MCS Control-modulet.

## Hardware (v1)

- **Board:** Waveshare ESP32-S3-ETH
- **Chip:** ESP32-S3R8 (dual-core 240MHz, 512KB SRAM, 8MB PSRAM OPI)
- **Flash:** 16MB (W25Q128, QIO)
- **Ethernet:** W5500 via SPI (GPIO 11/12/13/14, RST=9, INT=10)
- **USB:** Type-C med CDC (upload + serial debug)
- **Produkt-link:** https://www.waveshare.com/esp32-s3-eth.htm
- **Wiki:** https://www.waveshare.com/wiki/ESP32-S3-ETH

## I2C Devices

| Address | Chip | Funktion |
|---------|------|----------|
| 0x21 | TCA9535 | Voltage select (5V/12V/24V) + HW/module version |
| 0x23 | TCA9535 | Input config channel B (analog front-end switches) |
| 0x25 | TCA9535 | Input config channel A (analog front-end switches) |
| 0x27 | TCA9535 | Serial control (RS-232/RS-485) + 2x relæ |
| 0x48 | ADS1115 | 16-bit ADC channel A (A1–A4) |
| 0x49 | ADS1115 | 16-bit ADC channel B (B1–B4) |

## Projektstruktur

```
src/
├── main.cpp              ← setup + FreeRTOS task start
├── board/                ← pin-definitions, board-specifikt
├── sampler/              ← 4 kHz ISR pulse sampling + flow guard
├── logging/              ← leveled log system (serial + ringbuffer)
├── debug/                ← diagnostik, serial commands, task registry
└── drivers/              ← hardware drivers
    ├── all_drivers_init  ← init af alt med probe + fejlhåndtering
    ├── hw_status         ← centralt register over hvad der er online
    ├── i2c               ← bus init + probe
    ├── voltage_select    ← spændingsvalg via 74HC139 demux
    ├── version           ← hardware + module version readout
    ├── serial_control    ← RS-232/RS-485 mode-skift
    ├── relays            ← 2x relæ kontrol
    ├── input_config      ← analog front-end switch kontrol
    ├── adc               ← ADS1115 voltage/mA/differential måling
    ├── buttons           ← 5-knap analog ladder
    ├── buzzer            ← PWM toner + melodier
    └── oled              ← SSD1306 128x64 display
```

## Arkitektur

### FreeRTOS (ikke custom scheduler)
Alt kører som selvstændige FreeRTOS tasks. Arduino's `loop()` er suspenderet.
FreeRTOS er standard på ESP32 og veldokumenteret — nemt for alle at læse op på.

### Memory-strategi
| Type | Bruges til |
|------|-----------|
| Intern SRAM (320 KB) | Tidskritisk: sampler, flow guard, drivers, task stacks |
| PSRAM (8 MB) | Ikke-tidskritisk: JSON parsing, interface-buffere (Niklas), store data |
| LittleFS (flash) | Persistent config (overlever genstart) |

Beslutning: Interface-laget (JSON kommunikation mellem Jesper/Niklas) allokerer buffere i PSRAM via `ps_malloc()`.
Tidskritisk kode bruger kun intern RAM.

### Memory reference

| Type | Størrelse | Hastighed | Volatile | Bruges til |
|------|-----------|-----------|----------|-----------|
| Internal SRAM | 320 KB | 1 clock cycle | Ja | Task stacks, variabler, buffere, FreeRTOS kernel |
| PSRAM (OPI) | 8 MB | ~10x langsommere (SPI) | Ja | Store buffere, JSON, data der ikke er tidskritisk |
| RTC SRAM | 16 KB | Hurtig | Overlever deep sleep | Variabler der skal overleve sleep (wake-up state) |
| Flash (XIP) | 16 MB | Langsom (cachet) | Nej | Firmware, LittleFS, NVS, OTA partitions |
| ↳ LittleFS | 5.8 MB | " | Nej | Filer (config JSON, logs, assets) |
| ↳ NVS | ~20 KB | " | Nej | Key-value store (WiFi creds, små config-værdier) |
| eFuse | 256 bytes | Engangslæs | Nej, permanent | MAC-adresse, security keys (read-only) |

**Tommelfingerregler:**
- Skal det være hurtigt → intern SRAM
- Skal det være stort → PSRAM
- Skal det overleve genstart → Flash (LittleFS eller NVS)
- Skal det overleve deep sleep → RTC SRAM

**Eksempler:**

```cpp
// --- Intern SRAM (default, alt normalt) ---
uint8_t buffer[256];                    // stack — automatisk intern
char* ptr = (char*)malloc(1024);        // heap — automatisk intern

// --- PSRAM (store ting, ikke tidskritisk) ---
char* json_buf = (char*)ps_malloc(8192);              // allokér 8 KB i PSRAM
uint8_t* big = (uint8_t*)heap_caps_malloc(50000, MALLOC_CAP_SPIRAM);  // eksplicit PSRAM

// --- RTC SRAM (overlever deep sleep) ---
RTC_DATA_ATTR uint32_t boot_count = 0;  // bevares gennem deep sleep cycles

// --- LittleFS (filer i flash, overlever genstart) ---
#include <LittleFS.h>
LittleFS.begin();
File f = LittleFS.open("/config.json", "r");
String content = f.readString();
f.close();

// --- NVS (key-value, overlever genstart) ---
#include <Preferences.h>
Preferences prefs;
prefs.begin("config", false);           // namespace "config"
prefs.putString("ssid", "MyWiFi");      // skriv
String ssid = prefs.getString("ssid");  // læs
prefs.end();
```

### TBD
- [ ] PSRAM-allocator til ArduinoJson — så `JsonDocument` automatisk bruger PSRAM uden rå pointers

## Toolchain

- PlatformIO (VS Code)
- Framework: Arduino
- Partition: Dual OTA (2x 5MB app) + LittleFS (5.8MB)

## Ansvarsfordeling

- **Jesper:** Hardware-lag — I/O, pulsmåling, relæer, sensorer, OLED/menu, lokal config/storage
- **Niklas:** Kommunikation — WiFi, Ethernet, BLE, USB serial, ESP-NOW, server-forbindelse

## Fejltolerance

Alle I2C-drivers probes ved boot. Hvis en chip ikke svarer:
- Den logges som fejl
- `hw_available()` returnerer false
- Driver-funktioner returnerer tidligt (0 / no-op)
- Systemet kører videre med det der virker

## Byg

1. Åbn mappen i VS Code med PlatformIO installeret
2. Build (flueben i bunden eller `pio run`)
3. Upload til board (pil i bunden eller `pio run -t upload`)
4. Serial Monitor: 115200 baud
