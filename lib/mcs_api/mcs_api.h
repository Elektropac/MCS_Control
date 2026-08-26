#pragma once
// =======================================================
// MCS API — the only header an app needs
// =======================================================
// This wraps all hardware access and message bus functions.
// Apps include ONLY this file. Nothing else.
// =======================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "mcs_bus.h"

// -------------------------------------------------------
// Input modes
// -------------------------------------------------------
enum McsInputMode {
    MCS_MODE_OFF = 0,
    MCS_MODE_VOLTAGE,         // 0-5V via divider → ADC
    MCS_MODE_CURRENT,         // 4-20mA via 200Ω shunt → ADC
    MCS_MODE_DIGITAL,         // opto-isolated digital input
    MCS_MODE_DIGITAL_PULLUP,  // digital with internal pullup
    MCS_MODE_PULSE,           // digital + pullup + sampler tracking
};

// -------------------------------------------------------
// Buzzer sounds
// -------------------------------------------------------
enum McsBuzzerSound {
    MCS_SOUND_OK = 1,
    MCS_SOUND_ERROR,
    MCS_SOUND_STARTUP,
    MCS_SOUND_CLICK,
    MCS_SOUND_WARNING,
    MCS_SOUND_COMPLETE,
};

// -------------------------------------------------------
// Log levels
// -------------------------------------------------------
enum McsLogLevel {
    MCS_LOG_DEBUG,
    MCS_LOG_INFO,
    MCS_LOG_WARN,
    MCS_LOG_ERROR,
};

// -------------------------------------------------------
// Inputs — Reading
// -------------------------------------------------------
int32_t  mcs_adc_read_mv(uint8_t channel);    // 0-7
float    mcs_adc_read_ma(uint8_t channel);     // 0-7
bool     mcs_digital_read(uint8_t channel);    // 0-7
uint32_t mcs_pulse_count(uint8_t channel);     // 0-7
void     mcs_pulse_reset(uint8_t channel);

// -------------------------------------------------------
// Inputs — Configuration
// -------------------------------------------------------
void mcs_input_mode(uint8_t channel, McsInputMode mode);

// -------------------------------------------------------
// Outputs
// -------------------------------------------------------
void mcs_relay_set(uint8_t relay, bool state);   // 0=A, 1=B
bool mcs_relay_get(uint8_t relay);
void mcs_voltage_set(uint8_t channel_ab, uint8_t voltage);  // channel 0=A, 1=B; voltage 0/5/12/24

// -------------------------------------------------------
// Message Bus (pub/sub between apps and network)
// -------------------------------------------------------
inline void mcs_publish(const char* topic, const JsonDocument& data) {
    mcs_bus::publish(topic, data);
}

inline int mcs_subscribe(const char* topic, void(*cb)(const char* topic, const JsonObject& data)) {
    return mcs_bus::subscribe(topic, cb);
}

inline void mcs_process_inbox() {
    mcs_bus::process_inbox();
}

// -------------------------------------------------------
// System
// -------------------------------------------------------
void     mcs_buzzer(McsBuzzerSound sound);
void     mcs_log(McsLogLevel level, const char* tag, const char* fmt, ...);
uint32_t mcs_uptime_ms();
