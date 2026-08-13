#include "version.h"
#include <Wire.h>

#define TCA9535_ADDR    0x21
#define REG_INPUT_1     0x01

static uint8_t read_port1() {
    Wire.beginTransmission(TCA9535_ADDR);
    Wire.write(REG_INPUT_1);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TCA9535_ADDR, (uint8_t)1);
    return Wire.read();
}

uint8_t version_hardware() {
    return read_port1() & 0x0F;  // P10–P13
}

uint8_t version_module() {
    return (read_port1() >> 4) & 0x0F;  // P14–P17
}
