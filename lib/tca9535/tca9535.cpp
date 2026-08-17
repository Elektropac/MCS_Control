#include "tca9535.h"

// TCA9535 register map
#define REG_INPUT_0     0x00
#define REG_INPUT_1     0x01
#define REG_OUTPUT_0    0x02
#define REG_OUTPUT_1    0x03
#define REG_POLARITY_0  0x04
#define REG_POLARITY_1  0x05
#define REG_CONFIG_0    0x06
#define REG_CONFIG_1    0x07

TCA9535::TCA9535(uint8_t addr) : _addr(addr) {}

void TCA9535::write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t TCA9535::read_reg(uint8_t reg) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(_addr, (uint8_t)1);
    return Wire.read();
}

void TCA9535::set_port_direction(uint8_t port, uint8_t direction) {
    write_reg(port == 0 ? REG_CONFIG_0 : REG_CONFIG_1, direction);
}

void TCA9535::write_port(uint8_t port, uint8_t value) {
    write_reg(port == 0 ? REG_OUTPUT_0 : REG_OUTPUT_1, value);
}

uint8_t TCA9535::read_port(uint8_t port) {
    return read_reg(port == 0 ? REG_INPUT_0 : REG_INPUT_1);
}

uint8_t TCA9535::read_output(uint8_t port) {
    return read_reg(port == 0 ? REG_OUTPUT_0 : REG_OUTPUT_1);
}

void TCA9535::set_bit(uint8_t port, uint8_t bit, bool high) {
    uint8_t reg = port == 0 ? REG_OUTPUT_0 : REG_OUTPUT_1;
    uint8_t val = read_reg(reg);
    if (high) val |= (1 << bit);
    else      val &= ~(1 << bit);
    write_reg(reg, val);
}
