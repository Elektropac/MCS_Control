#pragma once
// =======================================================
// VERSION — hardware and module version readout
// =======================================================
//
// Read from TCA9535 (0x21) Port 1:
//   P10–P13 = Hardware version (hardcoded via pull-ups/downs)
//   P14–P17 = Module version   (hardcoded via pull-ups/downs)
//
// Current board (v1): both read as 0001 (= 1)
//
// =======================================================
#include <Arduino.h>

// Read hardware version (4-bit)
uint8_t version_hardware();

// Read module version (4-bit)
uint8_t version_module();
