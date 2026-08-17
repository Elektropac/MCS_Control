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
#define OLED_DC         40
#define OLED_RST        39
#define OLED_CLK        38
#define OLED_DIN        37
#define OLED_CS         36

// -------------------------------------------------------
// I2C
// -------------------------------------------------------
#define I2C_SCL         34
#define I2C_SDA         33

// -------------------------------------------------------
// UART A — RS232/RS485 channel A
// -------------------------------------------------------
#define UART_A_TX       1
#define UART_A_RX       2

// -------------------------------------------------------
// UART B — RS232/RS485 channel B
// -------------------------------------------------------
#define UART_B_TX       43
#define UART_B_RX       44

// -------------------------------------------------------
// DIGITAL INPUTS — A side
// -------------------------------------------------------
#define A1_DIGITAL      48
#define A2_DIGITAL      17
#define A3_DIGITAL      16
#define A4_DIGITAL      18

// -------------------------------------------------------
// DIGITAL INPUTS — B side
// -------------------------------------------------------
#define B1_DIGITAL      35
#define B2_DIGITAL      41
#define B3_DIGITAL      42
#define B4_DIGITAL      47

// -------------------------------------------------------
// BUTTON
// -------------------------------------------------------
#define PIN_BUTTON      15

// -------------------------------------------------------
// BUZZER
// -------------------------------------------------------
#define PIN_BUZZER      21

// -------------------------------------------------------
// I2C DEVICE ADDRESSES
// -------------------------------------------------------
#define ADDR_VOLTAGE_SELECT  0x21
#define ADDR_INPUT_CONFIG_B  0x23
#define ADDR_INPUT_CONFIG_A  0x25
#define ADDR_SERIAL_CONTROL  0x27
#define ADDR_ADC_A           0x48
#define ADDR_ADC_B           0x49
