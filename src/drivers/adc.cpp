#include "adc.h"
#include <Wire.h>

// ------------------------------------------
// I2C addresses
// ------------------------------------------
#define ADS_ADDR_A  0x48    // Channel A
#define ADS_ADDR_B  0x49    // Channel B

// ------------------------------------------
// ADS1115 registers
// ------------------------------------------
#define REG_CONVERSION  0x00
#define REG_CONFIG      0x01

// ------------------------------------------
// ADS1115 config register bits
// ------------------------------------------
#define CFG_OS_START    0x8000  // start single conversion
#define CFG_MODE_SINGLE 0x0100  // single-shot mode
#define CFG_DR_128SPS   0x0080  // 128 samples/sec (default)
#define CFG_PGA_4096    0x0200  // ±4.096V range (1 bit = 0.125mV)

// MUX values for single-ended (AINx vs GND)
#define CFG_MUX_AIN0    0x4000
#define CFG_MUX_AIN1    0x5000
#define CFG_MUX_AIN2    0x6000
#define CFG_MUX_AIN3    0x7000

// ------------------------------------------
// Voltage divider ratio (÷2)
// ------------------------------------------
#define DIVIDER_RATIO   2

// ------------------------------------------
// Input mapping: AdcInput → {address, mux}
// ------------------------------------------
struct AdcMap {
    uint8_t  addr;
    uint16_t mux;
};

static const AdcMap ADC_MAP[8] = {
    // Channel A (0x48)
    { ADS_ADDR_A, CFG_MUX_AIN0 },  // ADC_A1 → ain0
    { ADS_ADDR_A, CFG_MUX_AIN1 },  // ADC_A2 → ain1
    { ADS_ADDR_A, CFG_MUX_AIN3 },  // ADC_A3 → ain3
    { ADS_ADDR_A, CFG_MUX_AIN2 },  // ADC_A4 → ain2
    // Channel B (0x49)
    { ADS_ADDR_B, CFG_MUX_AIN2 },  // ADC_B1 → ain2
    { ADS_ADDR_B, CFG_MUX_AIN3 },  // ADC_B2 → ain3
    { ADS_ADDR_B, CFG_MUX_AIN0 },  // ADC_B3 → ain0
    { ADS_ADDR_B, CFG_MUX_AIN1 },  // ADC_B4 → ain1
};

// ------------------------------------------
// Low-level I2C
// ------------------------------------------
static void write_register(uint8_t addr, uint8_t reg, uint16_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    Wire.endTransmission();
}

static int16_t read_register(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)2);
    int16_t val = (Wire.read() << 8) | Wire.read();
    return val;
}

static bool conversion_ready(uint8_t addr) {
    int16_t cfg = read_register(addr, REG_CONFIG);
    return (cfg & CFG_OS_START) != 0;  // OS bit = 1 when done
}

// ------------------------------------------
// Public API
// ------------------------------------------

void adc_init() {
    // Nothing special needed — ADS1115 is ready after power-on
    // Each read triggers a single-shot conversion
}

int16_t adc_read_raw_mv(AdcInput input) {
    const AdcMap &map = ADC_MAP[input];

    // Build config: single-shot, single-ended, ±4.096V, 128SPS
    uint16_t config = CFG_OS_START | map.mux | CFG_PGA_4096 |
                      CFG_MODE_SINGLE | CFG_DR_128SPS;

    write_register(map.addr, REG_CONFIG, config);

    // Wait for conversion (max ~8ms at 128SPS)
    unsigned long start = millis();
    while (!conversion_ready(map.addr)) {
        if (millis() - start > 20) break;  // timeout
    }

    // Read result — at PGA ±4.096V, 1 LSB = 0.125mV
    int16_t raw = read_register(map.addr, REG_CONVERSION);

    // Convert to millivolts (raw * 0.125)
    int32_t mv = ((int32_t)raw * 125) / 1000;

    return (int16_t)mv;
}

int32_t adc_read_mv(AdcInput input) {
    // Compensate for voltage divider
    return (int32_t)adc_read_raw_mv(input) * DIVIDER_RATIO;
}

float adc_read_ma(AdcInput input, float shunt_ohms) {
    // Voltage across shunt in mV, then I = V/R
    float mv = (float)adc_read_raw_mv(input) * DIVIDER_RATIO;
    return mv / shunt_ohms;
}

// ------------------------------------------
// Differential pairs
// ------------------------------------------
// ADS1115 MUX values for differential:
#define CFG_MUX_DIFF_01  0x0000  // AIN0 - AIN1
#define CFG_MUX_DIFF_23  0x3000  // AIN2 - AIN3

struct DiffMap {
    uint8_t  addr;
    uint16_t mux;
};

static const DiffMap DIFF_MAP[4] = {
    { ADS_ADDR_A, CFG_MUX_DIFF_01 },  // ADC_DIFF_A12: A1 vs A2
    { ADS_ADDR_A, CFG_MUX_DIFF_23 },  // ADC_DIFF_A34: A4 vs A3
    { ADS_ADDR_B, CFG_MUX_DIFF_01 },  // ADC_DIFF_B12: B3 vs B4
    { ADS_ADDR_B, CFG_MUX_DIFF_23 },  // ADC_DIFF_B34: B1 vs B2
};

int32_t adc_read_diff_mv(AdcDiffPair pair) {
    const DiffMap &map = DIFF_MAP[pair];

    uint16_t config = CFG_OS_START | map.mux | CFG_PGA_4096 |
                      CFG_MODE_SINGLE | CFG_DR_128SPS;

    write_register(map.addr, REG_CONFIG, config);

    unsigned long start = millis();
    while (!conversion_ready(map.addr)) {
        if (millis() - start > 20) break;
    }

    int16_t raw = read_register(map.addr, REG_CONVERSION);
    int32_t mv = ((int32_t)raw * 125) / 1000;

    return mv * DIVIDER_RATIO;
}
