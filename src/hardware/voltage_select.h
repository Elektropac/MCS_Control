#pragma once
#include <Arduino.h>

enum Voltage : uint8_t {
    VOLTAGE_OFF,
    VOLTAGE_5V,
    VOLTAGE_12V,
    VOLTAGE_24V
};

void voltage_select_init();
void voltage_select_set_a(Voltage v);
void voltage_select_set_b(Voltage v);
