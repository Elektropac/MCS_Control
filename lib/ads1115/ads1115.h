#pragma once
// =======================================================
// ADS1115 — 16-bit I2C ADC driver
// =======================================================
// Generic driver. Does NOT take I2C mutex — caller must
// wrap calls with i2c_take()/i2c_give() from hal.h.
//
// Usage:
//   ADS1115 adc(0x48);
//   int16_t raw = adc.read_single(0);     // AIN0, returns raw value
//   int16_t raw = adc.read_diff(0, 1);    // AIN0 - AIN1
//
// =======================================================
#include <Arduino.h>
#include <Wire.h>

// PGA (gain) settings — determines voltage range
enum ADS1115_PGA : uint16_t {
    PGA_6144  = 0x0000,   // ±6.144V  (1 bit = 0.1875 mV)
    PGA_4096  = 0x0200,   // ±4.096V  (1 bit = 0.125 mV)
    PGA_2048  = 0x0400,   // ±2.048V  (1 bit = 0.0625 mV)  [default]
    PGA_1024  = 0x0600,   // ±1.024V
    PGA_0512  = 0x0800,   // ±0.512V
    PGA_0256  = 0x0A00,   // ±0.256V
};

// Data rate settings
enum ADS1115_DR : uint16_t {
    DR_8SPS   = 0x0000,
    DR_16SPS  = 0x0020,
    DR_32SPS  = 0x0040,
    DR_64SPS  = 0x0060,
    DR_128SPS = 0x0080,   // [default]
    DR_250SPS = 0x00A0,
    DR_475SPS = 0x00C0,
    DR_860SPS = 0x00E0,
};

class ADS1115 {
public:
    ADS1115(uint8_t addr);

    // Set PGA and data rate (persist for subsequent reads)
    void set_pga(ADS1115_PGA pga);
    void set_data_rate(ADS1115_DR dr);

    // Read single-ended (channel 0-3 vs GND). Returns raw 16-bit value.
    int16_t read_single(uint8_t channel);

    // Read differential pair. Returns raw 16-bit value.
    // Pairs: (0,1), (0,3), (1,3), (2,3)
    int16_t read_diff(uint8_t pos, uint8_t neg);

    // Convert raw value to millivolts based on current PGA setting
    float raw_to_mv(int16_t raw);

    // Get address
    uint8_t address() const { return _addr; }

private:
    uint8_t _addr;
    ADS1115_PGA _pga;
    ADS1115_DR _dr;

    int16_t start_and_read(uint16_t mux_bits);
    void write_reg(uint8_t reg, uint16_t value);
    int16_t read_reg(uint8_t reg);
    bool conversion_ready();
};
