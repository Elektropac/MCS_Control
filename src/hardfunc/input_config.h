#pragma once
#include <Arduino.h>

enum Input : uint8_t {
    INPUT_A1, INPUT_A2, INPUT_A3, INPUT_A4,
    INPUT_B1, INPUT_B2, INPUT_B3, INPUT_B4
};

enum InputSwitch : uint8_t { SW_ANALOG, SW_SHUNT, SW_PULLUP, SW_DIGITAL };

enum InputMode : uint8_t {
    MODE_OFF,
    MODE_VOLTAGE,
    MODE_MA,
    MODE_DIGITAL,
    MODE_DIGITAL_PU,
    MODE_SSR_PULLUP,
    MODE_SSR_SHUNT
};

void input_config_init();
void input_config_set(Input input, InputSwitch sw, bool on);
bool input_config_get(Input input, InputSwitch sw);
void input_config_mode(Input input, InputMode mode);
