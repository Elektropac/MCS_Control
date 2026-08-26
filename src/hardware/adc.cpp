#include "adc.h"
#include "hw_status.h"
#include "pins.h"
#include "i2c.h"
#include "ads1115.h"

static ADS1115 adc_a(ADDR_ADC_A);
static ADS1115 adc_b(ADDR_ADC_B);

// Divider ratio on Control board
#define DIVIDER_RATIO 2

// Channel mapping: AdcInput → {chip, channel}
struct AdcMap { ADS1115* chip; uint8_t channel; };

static AdcMap ADC_MAP[8] = {
    { &adc_a, 0 },  // A1
    { &adc_a, 1 },  // A2
    { &adc_a, 3 },  // A3
    { &adc_a, 2 },  // A4
    { &adc_b, 2 },  // B1
    { &adc_b, 3 },  // B2
    { &adc_b, 0 },  // B3
    { &adc_b, 1 },  // B4
};

// Differential mapping
struct DiffMap { ADS1115* chip; uint8_t pos; uint8_t neg; };

static DiffMap DIFF_MAP[4] = {
    { &adc_a, 0, 1 },  // A12: AIN0 - AIN1
    { &adc_a, 2, 3 },  // A34: AIN2 - AIN3
    { &adc_b, 0, 1 },  // B12: AIN0 - AIN1
    { &adc_b, 2, 3 },  // B34: AIN2 - AIN3
};

void adc_init() {
    adc_a.set_pga(PGA_4096);
    adc_a.set_data_rate(DR_128SPS);
    adc_b.set_pga(PGA_4096);
    adc_b.set_data_rate(DR_128SPS);
}

int16_t adc_read_raw_mv(AdcInput input) {
    if (input <= ADC_A4 && !hw_available(HW_ADC_A)) return 0;
    if (input >= ADC_B1 && !hw_available(HW_ADC_B)) return 0;

    if (!i2c_take(100)) return 0;

    AdcMap &map = ADC_MAP[input];
    int16_t raw = map.chip->read_single(map.channel);

    i2c_give();

    return (int16_t)(map.chip->raw_to_mv(raw));
}

int32_t adc_read_mv(AdcInput input) {
    return (int32_t)adc_read_raw_mv(input) * DIVIDER_RATIO;
}

float adc_read_ma(AdcInput input, float shunt_ohms) {
    float mv = (float)adc_read_raw_mv(input) * DIVIDER_RATIO;
    return mv / shunt_ohms;
}

int32_t adc_read_diff_mv(AdcDiffPair pair) {
    if (pair <= ADC_DIFF_A34 && !hw_available(HW_ADC_A)) return 0;
    if (pair >= ADC_DIFF_B12 && !hw_available(HW_ADC_B)) return 0;

    if (!i2c_take(100)) return 0;

    DiffMap &map = DIFF_MAP[pair];
    int16_t raw = map.chip->read_diff(map.pos, map.neg);

    i2c_give();

    int32_t mv = (int32_t)(map.chip->raw_to_mv(raw));
    return mv * DIVIDER_RATIO;
}
