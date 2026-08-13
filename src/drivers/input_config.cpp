#include "input_config.h"
#include <Wire.h>

// ------------------------------------------
// I2C addresses
// ------------------------------------------
#define ADDR_CHANNEL_A  0x25
#define ADDR_CHANNEL_B  0x23

// ------------------------------------------
// TCA9535 registers
// ------------------------------------------
#define REG_OUTPUT_0    0x02
#define REG_OUTPUT_1    0x03
#define REG_CONFIG_0    0x06
#define REG_CONFIG_1    0x07

// ------------------------------------------
// Bit mapping per input
// Each input has 4 switches at specific bit positions
// Format: {port (0 or 1), bit_analog, bit_shunt, bit_pullup, bit_digital}
// ------------------------------------------
struct InputMap {
    uint8_t port;
    uint8_t bit_analog;
    uint8_t bit_shunt;
    uint8_t bit_pullup;
    uint8_t bit_digital;
};

// Channel A (0x25)
static const InputMap MAP_A[4] = {
    // A1: Port 1 — P13=ANALOG, P11=SHUNT, P12=PULLUP, P10=DIGITAL
    { 1, 3, 1, 2, 0 },
    // A2: Port 1 — P17=ANALOG, P15=SHUNT, P16=PULLUP, P14=DIGITAL
    { 1, 7, 5, 6, 4 },
    // A3: Port 0 — P04=ANALOG, P06=SHUNT, P05=PULLUP, P07=DIGITAL
    { 0, 4, 6, 5, 7 },
    // A4: Port 0 — P00=ANALOG, P02=SHUNT, P01=PULLUP, P03=DIGITAL
    { 0, 0, 2, 1, 3 },
};

// Channel B (0x23)
static const InputMap MAP_B[4] = {
    // B1: Port 0 — P05=ANALOG, P07=SHUNT, P04=PULLUP, P06=DIGITAL
    { 0, 5, 7, 4, 6 },
    // B2: Port 0 — P01=ANALOG, P03=SHUNT, P00=PULLUP, P02=DIGITAL
    { 0, 1, 3, 0, 2 },
    // B3: Port 1 — P12=ANALOG, P10=SHUNT, P13=PULLUP, P11=DIGITAL
    { 1, 2, 0, 3, 1 },
    // B4: Port 1 — P16=ANALOG, P14=SHUNT, P17=PULLUP, P15=DIGITAL
    { 1, 6, 4, 7, 5 },
};

// ------------------------------------------
// State: mirror of output ports
// ------------------------------------------
static uint8_t s_a_port[2] = { 0, 0 };
static uint8_t s_b_port[2] = { 0, 0 };

// ------------------------------------------
// Low-level I2C
// ------------------------------------------
static void write_port(uint8_t addr, uint8_t port, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(port == 0 ? REG_OUTPUT_0 : REG_OUTPUT_1);
    Wire.write(value);
    Wire.endTransmission();
}

// ------------------------------------------
// Internal helpers
// ------------------------------------------
static uint8_t get_bit_for_switch(const InputMap &map, InputSwitch sw) {
    switch (sw) {
        case SW_ANALOG:  return map.bit_analog;
        case SW_SHUNT:   return map.bit_shunt;
        case SW_PULLUP:  return map.bit_pullup;
        case SW_DIGITAL: return map.bit_digital;
    }
    return 0;
}

static void apply_switch(uint8_t addr, uint8_t *ports, const InputMap &map, InputSwitch sw, bool on) {
    uint8_t bit = get_bit_for_switch(map, sw);
    if (on) ports[map.port] |= (1 << bit);
    else    ports[map.port] &= ~(1 << bit);
    write_port(addr, map.port, ports[map.port]);
}

// ------------------------------------------
// Public API
// ------------------------------------------

void input_config_init() {
    // Configure all pins as outputs on both expanders
    Wire.beginTransmission(ADDR_CHANNEL_A);
    Wire.write(REG_CONFIG_0);
    Wire.write(0x00);
    Wire.endTransmission();
    Wire.beginTransmission(ADDR_CHANNEL_A);
    Wire.write(REG_CONFIG_1);
    Wire.write(0x00);
    Wire.endTransmission();

    Wire.beginTransmission(ADDR_CHANNEL_B);
    Wire.write(REG_CONFIG_0);
    Wire.write(0x00);
    Wire.endTransmission();
    Wire.beginTransmission(ADDR_CHANNEL_B);
    Wire.write(REG_CONFIG_1);
    Wire.write(0x00);
    Wire.endTransmission();

    // All switches off
    s_a_port[0] = 0; s_a_port[1] = 0;
    s_b_port[0] = 0; s_b_port[1] = 0;

    write_port(ADDR_CHANNEL_A, 0, 0);
    write_port(ADDR_CHANNEL_A, 1, 0);
    write_port(ADDR_CHANNEL_B, 0, 0);
    write_port(ADDR_CHANNEL_B, 1, 0);
}

void input_config_set(Input input, InputSwitch sw, bool on) {
    if (input <= INPUT_A4) {
        apply_switch(ADDR_CHANNEL_A, s_a_port, MAP_A[input], sw, on);
    } else {
        apply_switch(ADDR_CHANNEL_B, s_b_port, MAP_B[input - INPUT_B1], sw, on);
    }
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
    // Clear all 4 switches first
    input_config_set(input, SW_ANALOG, false);
    input_config_set(input, SW_SHUNT, false);
    input_config_set(input, SW_PULLUP, false);
    input_config_set(input, SW_DIGITAL, false);

    // Set the combo
    switch (mode) {
        case MODE_OFF:
            break;
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
