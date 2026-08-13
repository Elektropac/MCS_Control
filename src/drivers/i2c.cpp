#include "i2c.h"
#include "board/pins.h"

void i2c_init() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);  // 400 kHz (Fast Mode)
}

bool i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}
