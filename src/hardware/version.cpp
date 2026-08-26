#include "version.h"
#include "pins.h"
#include "i2c.h"
#include "tca9535.h"

static TCA9535 expander(ADDR_VOLTAGE_SELECT);

uint8_t version_hardware() {
    if (!i2c_take(100)) return 0;
    uint8_t val = expander.read_port(1) & 0x0F;
    i2c_give();
    return val;
}

uint8_t version_module() {
    if (!i2c_take(100)) return 0;
    uint8_t val = (expander.read_port(1) >> 4) & 0x0F;
    i2c_give();
    return val;
}
