#pragma once
#include <Arduino.h>

enum SerialChannel : uint8_t { CHANNEL_A, CHANNEL_B };
enum ComMode : uint8_t { COM_OFF, COM_RS232, COM_RS485 };

void serial_control_init();
void serial_set_mode(SerialChannel ch, ComMode mode);
void serial_rs485_transmit(SerialChannel ch, bool enable);
void serial_rs485_termination(SerialChannel ch, bool enable);

// Query current state
ComMode serial_get_mode(SerialChannel ch);
bool serial_get_de(SerialChannel ch);
bool serial_get_re(SerialChannel ch);
bool serial_get_termination(SerialChannel ch);
