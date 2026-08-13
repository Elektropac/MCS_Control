#pragma once
// =======================================================
// ADC — ADS1115 16-bit analog-to-digital converters
// =======================================================
//
// Two ADS1115 chips, one per channel side:
//   Channel A (0x48): A1, A2, A4, A3
//   Channel B (0x49): B3, B4, B1, B2
//
// All inputs are single-ended (AINx vs GND).
//
// Hardware: voltage divider (÷2) + clamp at 5.2V
//   0–10V input → 0–5V at ADC
//   0–5V input  → 0–2.5V at ADC
//
// The driver returns raw ADC voltage (after gain) and
// also a scaled value compensating for the divider.
//
// =======================================================
#include <Arduino.h>

// Input identifiers (matches input_config.h order)
enum AdcInput : uint8_t {
    ADC_A1, ADC_A2, ADC_A3, ADC_A4,
    ADC_B1, ADC_B2, ADC_B3, ADC_B4,
};

// Initialize both ADCs
void adc_init();

// Read raw voltage at ADC pin (after divider), in millivolts
int16_t adc_read_raw_mv(AdcInput input);

// Read scaled voltage (compensated for divider), in millivolts
// This is the actual voltage at the input terminal
int32_t adc_read_mv(AdcInput input);

// Read as milliamps (for inputs configured with shunt)
// shunt_ohms: shunt resistor value (e.g. 100.0 for 100Ω)
float adc_read_ma(AdcInput input, float shunt_ohms);

// Differential pairs (AIN0-AIN1, AIN2-AIN3 on each chip)
enum AdcDiffPair : uint8_t {
    ADC_DIFF_A12,   // Channel A: AIN0–AIN1 (A1 vs A2)
    ADC_DIFF_A34,   // Channel A: AIN2–AIN3 (A4 vs A3)
    ADC_DIFF_B12,   // Channel B: AIN0–AIN1 (B3 vs B4)
    ADC_DIFF_B34,   // Channel B: AIN2–AIN3 (B1 vs B2)
};

// Read differential voltage in millivolts (compensated for divider)
int32_t adc_read_diff_mv(AdcDiffPair pair);
