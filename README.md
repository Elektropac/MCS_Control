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
├── main.cpp              ← setup + loop (clean, kun init + task_add)
├── board/                ← pin-definitions, board-specifikt
├── scheduler/            ← cooperative task scheduler
├── sampler/              ← 4 kHz ISR pulse sampling + flow guard
├── logging/              ← leveled log system (serial + ringbuffer)
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
