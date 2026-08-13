#pragma once
// =======================================================
// ALL DRIVERS INIT — initializes all hardware drivers
// =======================================================
//
// Probes each I2C device before init. If a device does not
// respond, it is skipped and logged. The system continues
// running with whatever hardware is available.
//
// =======================================================
#include <Arduino.h>

// Initialize all drivers. Call after i2c_init().
void all_drivers_init();
