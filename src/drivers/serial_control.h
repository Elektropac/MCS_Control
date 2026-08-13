#pragma once
// =======================================================
// SERIAL CONTROL — RS-232/RS-485 mode switching
// =======================================================
//
// Each channel (A and B) has:
//   - MAX3221 (RS-232 transceiver)
//   - MAX485  (RS-485 transceiver)
//
// Controlled via TCA9535 IO expander at 0x27.
// This driver selects which mode is active per channel
// and controls transmit/receive direction for RS-485.
//
// Modes:
//   SERIAL_OFF   — both transceivers disabled
//   SERIAL_RS232 — MAX3221 enabled, MAX485 disabled
//   SERIAL_RS485 — MAX485 enabled, MAX3221 disabled
//
// =======================================================
#include <Arduino.h>

enum ComMode : uint8_t {
    COM_OFF,
    COM_RS232,
    COM_RS485,
};

enum SerialChannel : uint8_t {
    CHANNEL_A,
    CHANNEL_B,
};

// Initialize serial control (call after i2c_init)
void serial_control_init();

// Set mode for a channel
void serial_set_mode(SerialChannel ch, ComMode mode);

// RS-485: enable transmit (call before sending)
void serial_rs485_transmit(SerialChannel ch, bool enable);

// RS-485: enable/disable termination bias
void serial_rs485_termination(SerialChannel ch, bool enable);
