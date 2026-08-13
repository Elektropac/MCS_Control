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

## Toolchain

- PlatformIO (VS Code)
- Framework: Arduino
- Partition: Dual OTA (2x 5MB app) + LittleFS (5.8MB)

## Ansvarsfordeling

- **Jesper:** Hardware-lag — I/O, pulsmåling, relæer, sensorer, OLED/menu, lokal config/storage
- **Niklas:** Kommunikation — WiFi, Ethernet, BLE, USB serial, ESP-NOW, server-forbindelse

## Byg

1. Åbn mappen i VS Code med PlatformIO installeret
2. Build (flueben i bunden eller `pio run`)
