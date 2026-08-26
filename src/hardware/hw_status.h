#pragma once
// =======================================================
// HW_STATUS — tracks which hardware is online
// =======================================================
#include <Arduino.h>

enum HwDevice : uint8_t {
    HW_VOLTAGE_SELECT,
    HW_INPUT_CONFIG_A,
    HW_INPUT_CONFIG_B,
    HW_SERIAL_CONTROL,
    HW_ADC_A,
    HW_ADC_B,
    HW_COUNT
};

void hw_set_available(HwDevice dev, bool available);
bool hw_available(HwDevice dev);
