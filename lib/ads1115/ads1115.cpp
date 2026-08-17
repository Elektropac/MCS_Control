#include "ads1115.h"

// Register addresses
#define REG_CONVERSION  0x00
#define REG_CONFIG      0x01

// Config bits
#define CFG_OS_START     0x8000
#define CFG_MODE_SINGLE  0x0100

// MUX values for single-ended
static const uint16_t MUX_SINGLE[4] = { 0x4000, 0x5000, 0x6000, 0x7000 };

// MUX values for differential
// (0,1)=0x0000, (0,3)=0x1000, (1,3)=0x2000, (2,3)=0x3000
static const uint16_t MUX_DIFF[4][4] = {
    { 0xFFFF, 0x0000, 0xFFFF, 0x1000 },  // pos=0: (0,1) and (0,3)
    { 0xFFFF, 0xFFFF, 0xFFFF, 0x2000 },  // pos=1: (1,3)
    { 0xFFFF, 0xFFFF, 0xFFFF, 0x3000 },  // pos=2: (2,3)
    { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },  // pos=3: none
};

// LSB size in microvolts for each PGA setting
static float pga_lsb_uv(ADS1115_PGA pga) {
    switch (pga) {
        case PGA_6144: return 187.5f;
        case PGA_4096: return 125.0f;
        case PGA_2048: return 62.5f;
        case PGA_1024: return 31.25f;
        case PGA_0512: return 15.625f;
        case PGA_0256: return 7.8125f;
        default:       return 125.0f;
    }
}

ADS1115::ADS1115(uint8_t addr) : _addr(addr), _pga(PGA_4096), _dr(DR_128SPS) {}

void ADS1115::set_pga(ADS1115_PGA pga) { _pga = pga; }
void ADS1115::set_data_rate(ADS1115_DR dr) { _dr = dr; }

void ADS1115::write_reg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    Wire.endTransmission();
}

int16_t ADS1115::read_reg(uint8_t reg) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(_addr, (uint8_t)2);
    return (Wire.read() << 8) | Wire.read();
}

bool ADS1115::conversion_ready() {
    int16_t cfg = read_reg(REG_CONFIG);
    return (cfg & CFG_OS_START) != 0;
}

int16_t ADS1115::start_and_read(uint16_t mux_bits) {
    uint16_t config = CFG_OS_START | mux_bits | (uint16_t)_pga |
                      CFG_MODE_SINGLE | (uint16_t)_dr;

    write_reg(REG_CONFIG, config);

    // Wait for conversion
    unsigned long start = millis();
    while (!conversion_ready()) {
        if (millis() - start > 20) break;
    }

    return read_reg(REG_CONVERSION);
}

int16_t ADS1115::read_single(uint8_t channel) {
    if (channel > 3) return 0;
    return start_and_read(MUX_SINGLE[channel]);
}

int16_t ADS1115::read_diff(uint8_t pos, uint8_t neg) {
    if (pos > 3 || neg > 3) return 0;
    uint16_t mux = MUX_DIFF[pos][neg];
    if (mux == 0xFFFF) return 0;  // invalid pair
    return start_and_read(mux);
}

float ADS1115::raw_to_mv(int16_t raw) {
    return (float)raw * pga_lsb_uv(_pga) / 1000.0f;
}
