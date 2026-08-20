#include "adc.h"
#include "hw_status.h"
#include "pins.h"
#include "hal.h"
#include "ads1115.h"
#include "input_config.h"
#include "logging.h"
#include "LittleFS.h"

static ADS1115 adc_a(ADDR_ADC_A);
static ADS1115 adc_b(ADDR_ADC_B);

// Divider ratio on Control board
#define DIVIDER_RATIO 2

// Zero-offset calibration (raw mV before divider ratio)
static int16_t s_offset[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

#define CAL_FILE "/adc_cal.json"
#define CAL_SAMPLES 8  // average N readings for stable offset

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

    // Load saved calibration if available
    adc_calibrate_load();
}

// Internal: read raw mV WITHOUT offset correction (for calibration)
static int16_t adc_read_raw_uncalibrated(AdcInput input) {
    if (input <= ADC_A4 && !hw_available(HW_ADC_A)) return 0;
    if (input >= ADC_B1 && !hw_available(HW_ADC_B)) return 0;

    if (!i2c_take(100)) return 0;

    AdcMap &map = ADC_MAP[input];
    int16_t raw = map.chip->read_single(map.channel);

    i2c_give();

    return (int16_t)(map.chip->raw_to_mv(raw));
}

int16_t adc_read_raw_mv(AdcInput input) {
    int16_t raw = adc_read_raw_uncalibrated(input);
    return raw - s_offset[input];
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

// --- Calibration ---

void adc_calibrate_zero() {
    // Save current input states so we can restore them
    // Set each input to: shunt ON only (200Ω to GND, nothing else)
    // This gives ~0V at ADC input
    // Read multiple samples and average for stable offset

    log_info("[adc_cal] Starting zero calibration...");

    for (uint8_t i = 0; i < 8; i++) {
        Input inp = (Input)i;

        // Turn off all switches first
        input_config_set(inp, SW_ANALOG, false);
        input_config_set(inp, SW_PULLUP, false);
        input_config_set(inp, SW_DIGITAL, false);
        // Turn on shunt only (200Ω to GND)
        input_config_set(inp, SW_SHUNT, true);
    }

    // Wait for settling
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read and average
    for (uint8_t i = 0; i < 8; i++) {
        AdcInput adc_in = (AdcInput)i;
        int32_t sum = 0;

        for (uint8_t s = 0; s < CAL_SAMPLES; s++) {
            sum += adc_read_raw_uncalibrated(adc_in);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        s_offset[i] = (int16_t)(sum / CAL_SAMPLES);
        log_info("[adc_cal] %s offset: %d mV (raw)", 
                 (i < 4) ? "A" : "B", s_offset[i]);
    }

    // Turn off all shunts
    for (uint8_t i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_SHUNT, false);
    }

    // Save to flash
    adc_calibrate_save();

    log_info("[adc_cal] Zero calibration complete.");
}

void adc_calibrate_save() {
    if (!LittleFS.begin(true)) {
        log_error("[adc_cal] LittleFS mount failed");
        return;
    }

    File f = LittleFS.open(CAL_FILE, "w");
    if (!f) {
        log_error("[adc_cal] Cannot write %s", CAL_FILE);
        return;
    }

    // Simple CSV: one offset per line
    for (uint8_t i = 0; i < 8; i++) {
        f.printf("%d\n", s_offset[i]);
    }
    f.close();

    log_info("[adc_cal] Offsets saved to %s", CAL_FILE);
}

void adc_calibrate_load() {
    if (!LittleFS.begin(true)) return;

    File f = LittleFS.open(CAL_FILE, "r");
    if (!f) {
        // No calibration file — offsets stay at 0
        return;
    }

    for (uint8_t i = 0; i < 8; i++) {
        String line = f.readStringUntil('\n');
        if (line.length() > 0) {
            s_offset[i] = line.toInt();
        }
    }
    f.close();

    log_info("[adc_cal] Loaded offsets from %s", CAL_FILE);
}

int16_t adc_get_offset(AdcInput input) {
    if (input > ADC_B4) return 0;
    return s_offset[input];
}
