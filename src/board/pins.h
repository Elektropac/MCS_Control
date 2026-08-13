#pragma once
// =======================================================
// PIN DEFINITIONS — Waveshare ESP32-S3-ETH (v1)
// Verified against Control PCB schematic 2026-08-13
// =======================================================

// -------------------------------------------------------
// ETHERNET W5500 (SPI) — internal, fixed on board
// -------------------------------------------------------
#define ETH_MISO        12
#define ETH_MOSI        11
#define ETH_SCK         13
#define ETH_CS          14
#define ETH_RST         9
#define ETH_INT         10

// -------------------------------------------------------
// SD CARD (SPI) — internal, fixed on board
// -------------------------------------------------------
#define SD_MISO         5
#define SD_MOSI         6
#define SD_CLK          7
#define SD_CS           4

// -------------------------------------------------------
// OLED DISPLAY (SPI)
// -------------------------------------------------------
#define OLED_DC         40    // pin 11
#define OLED_RST        39    // pin 12
#define OLED_CLK        38    // pin 14
#define OLED_DIN        37    // pin 15 (MOSI)
#define OLED_CS         36    // pin 16

// -------------------------------------------------------
// I2C
// -------------------------------------------------------
#define I2C_SCL         34    // pin 19
#define I2C_SDA         33    // pin 20

// -------------------------------------------------------
// UART A — RS232/RS485 channel A
// -------------------------------------------------------
#define UART_A_TX       1     // pin 25
#define UART_A_RX       2     // pin 26

// -------------------------------------------------------
// UART B — RS232/RS485 channel B
// -------------------------------------------------------
#define UART_B_TX       43    // pin 21
#define UART_B_RX       44    // pin 22

// -------------------------------------------------------
// DIGITAL INPUTS — A side
// -------------------------------------------------------
#define A1_DIGITAL      48    // pin 4
#define A2_DIGITAL      17    // pin 34
#define A3_DIGITAL      16    // pin 32
#define A4_DIGITAL      18    // pin 31

// -------------------------------------------------------
// DIGITAL INPUTS — B side
// -------------------------------------------------------
#define B1_DIGITAL      35    // pin 17
#define B2_DIGITAL      41    // pin 10
#define B3_DIGITAL      42    // pin 9
#define B4_DIGITAL      47    // pin 5

// -------------------------------------------------------
// BUTTON
// -------------------------------------------------------
#define PIN_BUTTON      15    // pin 29

// -------------------------------------------------------
// BUZZER
// -------------------------------------------------------
#define PIN_BUZZER      21    // pin 35

// -------------------------------------------------------
// NOT USED BY CONTROL (but active on board)
// -------------------------------------------------------
// GPIO20 (pin 1)  — USB D+
// GPIO19 (pin 2)  — USB D-
// GPIO0  (pin 24) — boot strap
// GPIO46 (pin 6)  — NC
// GPIO45 (pin 7)  — NC
// GPIO3  (pin 27) — NC

// =======================================================
// END OF FILE
// =======================================================
