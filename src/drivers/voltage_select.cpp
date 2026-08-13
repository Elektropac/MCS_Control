#include "voltage_select.h"
#include "hw_status.h"
#include <Wire.h>

// ------------------------------------------
// TCA9535 I2C address
// ------------------------------------------
#define TCA9535_ADDR    0x21

// ------------------------------------------
// TCA9535 register addresses
// ------------------------------------------
#define REG_INPUT_0     0x00
#define REG_INPUT_1     0x01
#define REG_OUTPUT_0    0x02
#define REG_OUTPUT_1    0x03
#define REG_POLARITY_0  0x04
#define REG_POLARITY_1  0x05
#define REG_CONFIG_0    0x06
#define REG_CONFIG_1    0x07

// ------------------------------------------
// State: current output port 0 value
// ------------------------------------------
static uint8_t s_port0 = 0xFF;  // all high = all disabled (enable is active low)

// ------------------------------------------
// Low-level I2C helpers
// ------------------------------------------
static void write_register(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TCA9535_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

static uint8_t read_register(uint8_t reg) {
    Wire.beginTransmission(TCA9535_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TCA9535_ADDR, (uint8_t)1);
    return Wire.read();
}

// ------------------------------------------
// Public API
// ------------------------------------------

void voltage_select_init() {
    // Port 0 = all outputs (0x00 = output)
    write_register(REG_CONFIG_0, 0x00);

    // Port 1 = all inputs (0xFF = input)
    write_register(REG_CONFIG_1, 0xFF);

    // Start with all outputs high (enables inactive, selects don't matter)
    s_port0 = 0xFF;
    write_register(REG_OUTPUT_0, s_port0);
}

void voltage_select_set_a(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;

    // Clear A bits (P00=enable, P01=bit0, P02=bit1)
    s_port0 |= 0x07;  // disable: set P00 high (1G# inactive)

    if (v == VOLTAGE_OFF) {
        // Just leave enable high (disabled)
    } else {
        uint8_t sel = 0;
        switch (v) {
            case VOLTAGE_24V: sel = 0b01; break;  // A=1, B=0
            case VOLTAGE_12V: sel = 0b10; break;  // A=0, B=1
            case VOLTAGE_5V:  sel = 0b11; break;  // A=1, B=1
            default: break;
        }
        // Set select bits (P01, P02)
        s_port0 = (s_port0 & ~0x06) | ((sel & 0x03) << 1);
        // Enable: P00 LOW
        s_port0 &= ~0x01;
    }

    write_register(REG_OUTPUT_0, s_port0);
}

void voltage_select_set_b(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;

    // Clear B bits (P05=enable, P06=bit0, P07=bit1)
    s_port0 |= 0xE0;  // disable: set P05 high (2G# inactive)

    if (v == VOLTAGE_OFF) {
        // Just leave enable high (disabled)
    } else {
        uint8_t sel = 0;
        switch (v) {
            case VOLTAGE_24V: sel = 0b01; break;  // A=1, B=0
            case VOLTAGE_12V: sel = 0b10; break;  // A=0, B=1
            case VOLTAGE_5V:  sel = 0b11; break;  // A=1, B=1
            default: break;
        }
        // Set select bits (P06, P07)
        s_port0 = (s_port0 & ~0xC0) | ((sel & 0x03) << 6);
        // Enable: P05 LOW
        s_port0 &= ~0x20;
    }

    write_register(REG_OUTPUT_0, s_port0);
}
