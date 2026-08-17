#pragma once
// =======================================================
// SAMPLER — high-speed digital input sampling via ISR
// =======================================================
#include <Arduino.h>

struct SamplerEvent {
    uint32_t sample_index;
    uint8_t  state;   // bit0=A1..bit7=B4
};

void sampler_init();
bool sampler_read_next(uint32_t &cursor, SamplerEvent &out);
bool sampler_ready();
uint8_t sampler_current_state();
uint32_t sampler_write_position();
uint32_t sampler_sample_index();
size_t sampler_buffer_size();
