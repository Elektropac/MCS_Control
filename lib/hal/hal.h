#pragma once
// =======================================================
// HAL — Hardware Abstraction Layer
// =======================================================
// Provides thread-safe I2C bus access via FreeRTOS mutex.
// Used by all drivers that communicate on the I2C bus.
//
// Usage:
//   #include "hal.h"
//
//   if (i2c_take(100)) {
//       Wire.beginTransmission(addr);
//       Wire.write(reg);
//       Wire.endTransmission();
//       i2c_give();
//   }
//
// =======================================================
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Initialize I2C bus + mutex. Call once with your SDA/SCL pins.
void i2c_init(uint8_t sda, uint8_t scl, uint32_t freq_hz = 400000);

// Probe an I2C address — returns true if device responds.
// Takes mutex internally.
bool i2c_probe(uint8_t addr);

// Take I2C bus (blocks up to timeout_ms). Returns true if acquired.
bool i2c_take(uint32_t timeout_ms = 100);

// Release I2C bus.
void i2c_give();
