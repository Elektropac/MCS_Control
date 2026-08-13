#pragma once
// =======================================================
// TCA9535 — 16-bit I2C IO expander (address 0x21)
// =======================================================
//
// Port 0 (outputs):
//   P00 = Voltage select A — enable (74HC139 1G#, active low)
//   P01 = Voltage select A — bit 0 (74HC139 1A)
//   P02 = Voltage select A — bit 1 (74HC139 1B)
//   P03 = NC
//   P04 = NC
//   P05 = Voltage select B — enable (74HC139 2G#, active low)
//   P06 = Voltage select B — bit 0 (74HC139 2A)
//   P07 = Voltage select B — bit 1 (74HC139 2B)
//
// Port 1 (inputs):
//   P10–P13 = Hardware version (v1 = 0001)
//   P14–P17 = Module version  (v1 = 0001)
//
// Voltage selection via 74HC139 demux:
//   A=0, B=0 → 1Y0 = NC (off)
//   A=1, B=0 → 1Y1 = 24V
//   A=0, B=1 → 1Y2 = 12V
//   A=1, B=1 → 1Y3 = 5V
//
// =======================================================
#include <Arduino.h>

// Voltage options for channel A and B
enum Voltage : uint8_t {
    VOLTAGE_OFF,
    VOLTAGE_24V,
    VOLTAGE_12V,
    VOLTAGE_5V,
};

// Initialize TCA9535 (configure port directions)
void voltage_select_init();

// Set supply voltage for channel A
void voltage_select_set_a(Voltage v);

// Set supply voltage for channel B
void voltage_select_set_b(Voltage v);
