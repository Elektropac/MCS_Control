#include "mcs_api.h"
#include "hardfunc/adc.h"
#include "hardfunc/input_config.h"
#include "hardfunc/relays.h"
#include "hardfunc/voltage_select.h"
#include "buzzer.h"
#include "logging.h"
#include "tasks/sampler.h"
#include "pins.h"

// -------------------------------------------------------
// Inputs — Reading
// -------------------------------------------------------

int32_t mcs_adc_read_mv(uint8_t channel) {
    if (channel > 7) return 0;
    return adc_read_mv((AdcInput)channel);
}

float mcs_adc_read_ma(uint8_t channel) {
    if (channel > 7) return 0.0f;
    int32_t mv = adc_read_mv((AdcInput)channel);
    return mv / 200.0f;  // 200Ω shunt: I = V/R
}

bool mcs_digital_read(uint8_t channel) {
    if (channel > 7) return false;
    const uint8_t pins[] = {A1_DIGITAL, A2_DIGITAL, A3_DIGITAL, A4_DIGITAL,
                            B1_DIGITAL, B2_DIGITAL, B3_DIGITAL, B4_DIGITAL};
    // Opto-isolated: LOW = active (inverted)
    return digitalRead(pins[channel]) == LOW;
}

uint32_t mcs_pulse_count(uint8_t channel) {
    // TODO: implement per-channel pulse counting from sampler ring buffer
    // For now returns 0
    (void)channel;
    return 0;
}

void mcs_pulse_reset(uint8_t channel) {
    // TODO: reset per-channel counter
    (void)channel;
}

// -------------------------------------------------------
// Inputs — Configuration
// -------------------------------------------------------

void mcs_input_mode(uint8_t channel, McsInputMode mode) {
    if (channel > 7) return;
    Input inp = (Input)channel;

    switch (mode) {
        case MCS_MODE_OFF:
            input_config_mode(inp, MODE_OFF);
            break;
        case MCS_MODE_VOLTAGE:
            input_config_mode(inp, MODE_VOLTAGE);
            break;
        case MCS_MODE_CURRENT:
            input_config_mode(inp, MODE_MA);
            break;
        case MCS_MODE_DIGITAL:
            input_config_set(inp, SW_ANALOG, false);
            input_config_set(inp, SW_PULLUP, false);
            input_config_set(inp, SW_SHUNT, false);
            input_config_set(inp, SW_DIGITAL, true);
            break;
        case MCS_MODE_DIGITAL_PULLUP:
            input_config_set(inp, SW_ANALOG, false);
            input_config_set(inp, SW_PULLUP, true);
            input_config_set(inp, SW_SHUNT, false);
            input_config_set(inp, SW_DIGITAL, true);
            break;
        case MCS_MODE_PULSE:
            input_config_set(inp, SW_ANALOG, false);
            input_config_set(inp, SW_PULLUP, true);
            input_config_set(inp, SW_SHUNT, false);
            input_config_set(inp, SW_DIGITAL, true);
            break;
    }
}

// -------------------------------------------------------
// Outputs
// -------------------------------------------------------

void mcs_relay_set(uint8_t relay, bool state) {
    if (relay == 0) relay_set(RELAY_A, state);
    else if (relay == 1) relay_set(RELAY_B, state);
}

bool mcs_relay_get(uint8_t relay) {
    if (relay == 0) return relay_get(RELAY_A);
    if (relay == 1) return relay_get(RELAY_B);
    return false;
}

void mcs_voltage_set(uint8_t channel_ab, uint8_t voltage) {
    Voltage v;
    switch (voltage) {
        case 5:  v = VOLTAGE_5V;  break;
        case 12: v = VOLTAGE_12V; break;
        case 24: v = VOLTAGE_24V; break;
        default: v = VOLTAGE_OFF; break;
    }
    if (channel_ab == 0) voltage_select_set_a(v);
    else voltage_select_set_b(v);
}

// -------------------------------------------------------
// System
// -------------------------------------------------------

void mcs_buzzer(McsBuzzerSound sound) {
    switch (sound) {
        case MCS_SOUND_OK:       buzzer_sound_ok();       break;
        case MCS_SOUND_ERROR:    buzzer_sound_error();    break;
        case MCS_SOUND_STARTUP:  buzzer_sound_startup();  break;
        case MCS_SOUND_CLICK:    buzzer_sound_click();    break;
        case MCS_SOUND_WARNING:  buzzer_sound_warning();  break;
        case MCS_SOUND_COMPLETE: buzzer_sound_complete(); break;
    }
}

void mcs_log(McsLogLevel level, const char* tag, const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LogLevel ll;
    switch (level) {
        case MCS_LOG_DEBUG: ll = LOG_DEBUG; break;
        case MCS_LOG_INFO:  ll = LOG_INFO;  break;
        case MCS_LOG_WARN:  ll = LOG_WARN;  break;
        case MCS_LOG_ERROR: ll = LOG_ERROR; break;
        default: ll = LOG_INFO;
    }
    log_write(ll, tag, buf);
}

uint32_t mcs_uptime_ms() {
    return millis();
}
