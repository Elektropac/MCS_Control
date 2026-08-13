#pragma once
// =======================================================
// HW STATUS — central registry of hardware availability
// =======================================================
//
// After all_drivers_init() probes each chip, the result
// is stored here. Any code that uses a driver should
// check hw_available() first.
//
// Usage:
//   if (hw_available(HW_ADC_A)) {
//       int32_t mv = adc_read_mv(ADC_A1);
//   }
//
// =======================================================
#include <Arduino.h>

enum HwDevice : uint8_t {
    HW_VOLTAGE_SELECT,    // TCA9535 @ 0x21
    HW_SERIAL_CONTROL,    // TCA9535 @ 0x27
    HW_INPUT_CONFIG_A,    // TCA9535 @ 0x25
    HW_INPUT_CONFIG_B,    // TCA9535 @ 0x23
    HW_ADC_A,            // ADS1115 @ 0x48
    HW_ADC_B,            // ADS1115 @ 0x49
    HW_OLED,             // SSD1306 (SPI)
    HW_BUZZER,           // GPIO buzzer
    HW_BUTTONS,          // ADC buttons
    HW_COUNT             // keep last
};

// Mark a device as available/unavailable (called by all_drivers_init)
void hw_set_available(HwDevice device, bool available);

// Check if a device is available
bool hw_available(HwDevice device);

// Get number of devices that failed
uint8_t hw_fail_count();
