#include "voltage_select.h"
#include "hw_status.h"
#include "pins.h"
#include "i2c.h"
#include "tca9535.h"

static TCA9535 expander(ADDR_VOLTAGE_SELECT);
static uint8_t s_port0 = 0xFF;

void voltage_select_init() {
    if (!i2c_take(100)) return;
    expander.set_port_direction(0, 0x00);  // port 0 = output
    expander.set_port_direction(1, 0xFF);  // port 1 = input (version)
    s_port0 = 0xFF;
    expander.write_port(0, s_port0);
    i2c_give();
}

void voltage_select_set_a(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;
    if (!i2c_take(100)) return;

    s_port0 |= 0x07;  // disable

    if (v != VOLTAGE_OFF) {
        uint8_t sel = 0;
        switch (v) {
            case VOLTAGE_24V: sel = 0b01; break;
            case VOLTAGE_12V: sel = 0b10; break;
            case VOLTAGE_5V:  sel = 0b11; break;
            default: break;
        }
        s_port0 = (s_port0 & ~0x06) | ((sel & 0x03) << 1);
        s_port0 &= ~0x01;  // enable
    }

    expander.write_port(0, s_port0);
    i2c_give();
}

void voltage_select_set_b(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;
    if (!i2c_take(100)) return;

    s_port0 |= 0xE0;  // disable (P05=1, P06=1, P07=1)

    if (v != VOLTAGE_OFF) {
        // Channel B pin layout (differs from A!):
        //   P05 = 2B (sel1), P06 = 2A (sel0), P07 = 2G# (enable, active low)
        // 74HC139 decoder: 2B 2A -> output
        //   0 1 -> 5V (2Y1),  1 0 -> 24V (2Y2),  1 1 -> 12V (2Y3)
        uint8_t p05, p06;
        switch (v) {
            case VOLTAGE_5V:  p05 = 0; p06 = 1; break;
            case VOLTAGE_24V: p05 = 1; p06 = 0; break;
            case VOLTAGE_12V: p05 = 1; p06 = 1; break;
            default:          p05 = 0; p06 = 0; break;
        }
        s_port0 = (s_port0 & ~0xE0) | (p05 << 5) | (p06 << 6) | (0 << 7);  // P07=0 = enable
    }

    expander.write_port(0, s_port0);
    i2c_give();
}
