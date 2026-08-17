#pragma once
// =======================================================
// TCA9535 — 16-bit I2C IO expander driver
// =======================================================
// Generic driver. Does NOT take I2C mutex — caller must
// wrap calls with i2c_take()/i2c_give() from hal.h.
//
// Usage:
//   TCA9535 expander(0x21);
//   expander.set_port_direction(0, 0x00);  // port 0 = all output
//   expander.write_port(0, 0xFF);
//   uint8_t val = expander.read_port(1);
//
// =======================================================
#include <Arduino.h>
#include <Wire.h>

class TCA9535 {
public:
    TCA9535(uint8_t addr);

    // Set port direction: 0x00 = all output, 0xFF = all input
    void set_port_direction(uint8_t port, uint8_t direction);

    // Write all 8 bits of a port
    void write_port(uint8_t port, uint8_t value);

    // Read all 8 bits of a port (input register)
    uint8_t read_port(uint8_t port);

    // Read output register (what was last written)
    uint8_t read_output(uint8_t port);

    // Set a single bit on a port (read-modify-write on output register)
    void set_bit(uint8_t port, uint8_t bit, bool high);

    // Get address (for debug)
    uint8_t address() const { return _addr; }

private:
    uint8_t _addr;

    void write_reg(uint8_t reg, uint8_t value);
    uint8_t read_reg(uint8_t reg);
};
