#pragma once
#include <Arduino.h>

enum AdcInput : uint8_t {
    ADC_A1, ADC_A2, ADC_A3, ADC_A4,
    ADC_B1, ADC_B2, ADC_B3, ADC_B4
};

enum AdcDiffPair : uint8_t {
    ADC_DIFF_A12,   // A1 vs A2
    ADC_DIFF_A34,   // A4 vs A3
    ADC_DIFF_B12,   // B3 vs B4
    ADC_DIFF_B34    // B1 vs B2
};

void adc_init();
int16_t adc_read_raw_mv(AdcInput input);
int32_t adc_read_mv(AdcInput input);
float adc_read_ma(AdcInput input, float shunt_ohms);
int32_t adc_read_diff_mv(AdcDiffPair pair);

// Calibration — zero offset
void adc_calibrate_zero();          // Runs zero-cal on all 8 channels (uses shunt to GND)
void adc_calibrate_load();          // Load saved offsets from LittleFS
void adc_calibrate_save();          // Save current offsets to LittleFS
int16_t adc_get_offset(AdcInput input);  // Get stored offset for a channel (mV, raw)
