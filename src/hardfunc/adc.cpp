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

// Gain correction factors (multiply after offset correction)
static float s_gain[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

// Last gain calibration result (for display/API)
static AdcGainResult s_gain_result = {};

#define CAL_FILE "/adc_cal.json"
#define GAIN_FILE "/adc_gain.json"
#define CAL_SAMPLES 8  // average N readings for stable offset
#define GAIN_SAMPLES 16 // more samples for gain (higher precision needed)

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
    adc_gain_load();
}

// Internal: read raw mV WITHOUT any correction (for calibration)
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
    // Apply zero offset
    float corrected = (float)(raw - s_offset[input]);
    // Apply gain correction
    corrected *= s_gain[input];
    return (int16_t)corrected;
}

int32_t adc_read_mv(AdcInput input) {
    int32_t raw_mv = (int32_t)adc_read_raw_mv(input) * DIVIDER_RATIO;
    // Switch compensation: corrects for CMOS analog switch Ron effect
    // Calibrated against Fluke 177 at 1-5V, max error ±3 mV
    #define SWITCH_GAIN   1.004528f
    #define SWITCH_OFFSET 33.9f
    return (int32_t)(raw_mv * SWITCH_GAIN + SWITCH_OFFSET);
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

// --- Zero Calibration ---

void adc_calibrate_zero() {
    log_info("[adc_cal] Starting zero calibration...");

    for (uint8_t i = 0; i < 8; i++) {
        Input inp = (Input)i;
        input_config_set(inp, SW_ANALOG, false);
        input_config_set(inp, SW_PULLUP, false);
        input_config_set(inp, SW_DIGITAL, false);
        input_config_set(inp, SW_SHUNT, true);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    for (uint8_t i = 0; i < 8; i++) {
        AdcInput adc_in = (AdcInput)i;
        int32_t sum = 0;

        for (uint8_t s = 0; s < CAL_SAMPLES; s++) {
            sum += adc_read_raw_uncalibrated(adc_in);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        s_offset[i] = (int16_t)(sum / CAL_SAMPLES);
        log_info("[adc_cal] %s%d offset: %d mV (raw)", 
                 (i < 4) ? "A" : "B", (i < 4) ? i+1 : i-3, s_offset[i]);
    }

    for (uint8_t i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_SHUNT, false);
    }

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

    for (uint8_t i = 0; i < 8; i++) {
        f.printf("%d\n", s_offset[i]);
    }
    f.close();

    log_info("[adc_cal] Offsets saved to %s", CAL_FILE);
}

void adc_calibrate_load() {
    if (!LittleFS.begin(true)) return;

    File f = LittleFS.open(CAL_FILE, "r");
    if (!f) return;

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

// --- Gain Calibration ---
// 
// Method (simple and correct):
//   1. Set pullup ON (all other OFF) on all 8 inputs, voltage at 5V
//      → All inputs see the same voltage via identical 5kΩ pullup
//      → Measure all 8 (after zero correction)
//      → Mean of all 8 = "true" value
//      → Per input: factor = mean / measured
//   
// That's it. Direct correction factor per channel.
// Requires voltage supply at 5V and NO external cables connected!

void adc_calibrate_gain() {
    log_info("[adc_gain] Starting gain calibration...");

    // Temporarily disable gain correction during calibration
    for (uint8_t i = 0; i < 8; i++) {
        s_gain[i] = 1.0f;
    }

    // Set pullup on all 8 inputs, analog switch on
    for (uint8_t i = 0; i < 8; i++) {
        Input inp = (Input)i;
        input_config_set(inp, SW_ANALOG, true);
        input_config_set(inp, SW_PULLUP, true);
        input_config_set(inp, SW_SHUNT, false);
        input_config_set(inp, SW_DIGITAL, false);
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // Let voltages settle

    // Measure all 8 channels (multiple samples, averaged)
    int32_t measured[8];
    for (uint8_t i = 0; i < 8; i++) {
        int32_t sum = 0;
        for (uint8_t s = 0; s < GAIN_SAMPLES; s++) {
            sum += adc_read_raw_uncalibrated((AdcInput)i) - s_offset[i];
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        measured[i] = sum / GAIN_SAMPLES;
        s_gain_result.high_mv[i] = measured[i];
    }

    // Compute mean of all 8
    int32_t total = 0;
    for (uint8_t i = 0; i < 8; i++) total += measured[i];
    int32_t mean = total / 8;

    s_gain_result.high_mean = mean;
    s_gain_result.low_mean = 0;  // Not used in simple method

    log_info("[adc_gain] Mean: %d mV", mean);

    // Compute gain factor per channel: factor = mean / measured
    for (uint8_t i = 0; i < 8; i++) {
        if (measured[i] > 100) {  // sanity check
            s_gain[i] = (float)mean / (float)measured[i];
        } else {
            s_gain[i] = 1.0f;
            log_error("[adc_gain] Ch %d: reading too low (%d mV), skipping", i, measured[i]);
        }

        s_gain_result.factor[i] = s_gain[i];
        s_gain_result.low_mv[i] = 0;

        log_info("[adc_gain] %s%d: measured=%d factor=%.4f", 
                 (i < 4) ? "A" : "B", (i < 4) ? i+1 : i-3,
                 measured[i], s_gain[i]);
    }

    // Cleanup: turn off all switches
    for (uint8_t i = 0; i < 8; i++) {
        input_config_mode((Input)i, MODE_OFF);
    }

    adc_gain_save();
    log_info("[adc_gain] Gain calibration complete.");
}

void adc_gain_save() {
    if (!LittleFS.begin(true)) {
        log_error("[adc_gain] LittleFS mount failed");
        return;
    }

    File f = LittleFS.open(GAIN_FILE, "w");
    if (!f) {
        log_error("[adc_gain] Cannot write %s", GAIN_FILE);
        return;
    }

    // One gain factor per line (float with 6 decimals)
    for (uint8_t i = 0; i < 8; i++) {
        f.printf("%.6f\n", s_gain[i]);
    }
    f.close();

    log_info("[adc_gain] Gain factors saved to %s", GAIN_FILE);
}

void adc_gain_load() {
    if (!LittleFS.begin(true)) return;

    File f = LittleFS.open(GAIN_FILE, "r");
    if (!f) return;

    for (uint8_t i = 0; i < 8; i++) {
        String line = f.readStringUntil('\n');
        if (line.length() > 0) {
            float val = line.toFloat();
            // Sanity: gain factor should be close to 1.0 (0.9 to 1.1)
            if (val > 0.9f && val < 1.1f) {
                s_gain[i] = val;
            } else {
                log_error("[adc_gain] Ch %d: loaded factor %.4f out of range, using 1.0", i, val);
                s_gain[i] = 1.0f;
            }
        }
    }
    f.close();

    log_info("[adc_gain] Loaded gain factors from %s", GAIN_FILE);
}

float adc_get_gain(AdcInput input) {
    if (input > ADC_B4) return 1.0f;
    return s_gain[input];
}

const AdcGainResult& adc_get_gain_result() {
    return s_gain_result;
}
