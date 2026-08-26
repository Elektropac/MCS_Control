#include "input_config.h"
#include "hw_status.h"
#include "pins.h"
#include "i2c.h"
#include "tca9535.h"

static TCA9535 exp_a(ADDR_INPUT_CONFIG_A);
static TCA9535 exp_b(ADDR_INPUT_CONFIG_B);

// Bit mapping per input: {port, bit_analog, bit_shunt, bit_pullup, bit_digital}
struct InputMap {
    uint8_t port;
    uint8_t bit_analog, bit_shunt, bit_pullup, bit_digital;
};

static const InputMap MAP_A[4] = {
    { 1, 3, 1, 2, 0 },  // A1
    { 1, 7, 5, 6, 4 },  // A2
    { 0, 4, 6, 5, 7 },  // A3
    { 0, 0, 2, 1, 3 },  // A4
};

static const InputMap MAP_B[4] = {
    { 0, 5, 7, 4, 6 },  // B1
    { 0, 1, 3, 0, 2 },  // B2
    { 1, 2, 0, 3, 1 },  // B3
    { 1, 6, 4, 7, 5 },  // B4
};

static uint8_t s_a_port[2] = { 0, 0 };
static uint8_t s_b_port[2] = { 0, 0 };

static uint8_t get_bit_for_switch(const InputMap &map, InputSwitch sw) {
    switch (sw) {
        case SW_ANALOG:  return map.bit_analog;
        case SW_SHUNT:   return map.bit_shunt;
        case SW_PULLUP:  return map.bit_pullup;
        case SW_DIGITAL: return map.bit_digital;
    }
    return 0;
}

void input_config_init() {
    if (!i2c_take(100)) return;
    exp_a.set_port_direction(0, 0x00);
    exp_a.set_port_direction(1, 0x00);
    exp_b.set_port_direction(0, 0x00);
    exp_b.set_port_direction(1, 0x00);

    s_a_port[0] = 0; s_a_port[1] = 0;
    s_b_port[0] = 0; s_b_port[1] = 0;

    exp_a.write_port(0, 0);
    exp_a.write_port(1, 0);
    exp_b.write_port(0, 0);
    exp_b.write_port(1, 0);
    i2c_give();
}

void input_config_set(Input input, InputSwitch sw, bool on) {
    if (input <= INPUT_A4 && !hw_available(HW_INPUT_CONFIG_A)) return;
    if (input >= INPUT_B1 && !hw_available(HW_INPUT_CONFIG_B)) return;

    if (!i2c_take(100)) return;

    if (input <= INPUT_A4) {
        const InputMap &map = MAP_A[input];
        uint8_t bit = get_bit_for_switch(map, sw);
        if (on) s_a_port[map.port] |= (1 << bit);
        else    s_a_port[map.port] &= ~(1 << bit);
        exp_a.write_port(map.port, s_a_port[map.port]);
    } else {
        const InputMap &map = MAP_B[input - INPUT_B1];
        uint8_t bit = get_bit_for_switch(map, sw);
        if (on) s_b_port[map.port] |= (1 << bit);
        else    s_b_port[map.port] &= ~(1 << bit);
        exp_b.write_port(map.port, s_b_port[map.port]);
    }

    i2c_give();
}

bool input_config_get(Input input, InputSwitch sw) {
    const InputMap *map;
    uint8_t *ports;

    if (input <= INPUT_A4) {
        map = &MAP_A[input];
        ports = s_a_port;
    } else {
        map = &MAP_B[input - INPUT_B1];
        ports = s_b_port;
    }

    uint8_t bit = get_bit_for_switch(*map, sw);
    return (ports[map->port] >> bit) & 0x01;
}

void input_config_mode(Input input, InputMode mode) {
    input_config_set(input, SW_ANALOG, false);
    input_config_set(input, SW_SHUNT, false);
    input_config_set(input, SW_PULLUP, false);
    input_config_set(input, SW_DIGITAL, false);

    switch (mode) {
        case MODE_OFF: break;
        case MODE_VOLTAGE:
            input_config_set(input, SW_ANALOG, true);
            break;
        case MODE_MA:
            input_config_set(input, SW_ANALOG, true);
            input_config_set(input, SW_SHUNT, true);
            break;
        case MODE_DIGITAL:
            input_config_set(input, SW_DIGITAL, true);
            break;
        case MODE_DIGITAL_PU:
            input_config_set(input, SW_DIGITAL, true);
            input_config_set(input, SW_PULLUP, true);
            break;
        case MODE_SSR_PULLUP:
            input_config_set(input, SW_PULLUP, true);
            break;
        case MODE_SSR_SHUNT:
            input_config_set(input, SW_SHUNT, true);
            break;
    }
}
