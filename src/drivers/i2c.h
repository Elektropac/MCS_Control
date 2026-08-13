#pragma once
// =======================================================
// I2C BUS — initialization and shared bus access
// =======================================================
//
// All I2C devices (IO expander, ADC, OLED, sensors) share
// this bus. Initialize once in setup(), then each driver
// uses Wire to talk to its device.
//
// =======================================================
#include <Arduino.h>
#include <Wire.h>

// Initialize I2C bus on the configured pins (SDA/SCL from pins.h)
void i2c_init();

// Probe an I2C address — returns true if device responds
bool i2c_probe(uint8_t addr);
