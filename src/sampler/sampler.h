#pragma once
// =======================================================
// SAMPLER — high-speed digital input sampling via ISR
// =======================================================
//
// WHY THIS EXISTS:
// Pulse inputs from flow meters can arrive at high frequency.
// Missing even one pulse means inaccurate measurement.
// The sampler uses a hardware timer interrupt (4 kHz) to
// read all digital inputs continuously, regardless of what
// else the MCU is doing. Only state changes are stored in
// a ring buffer for later processing.
//
// IMPORTANT:
// The sampler runs ALWAYS — whether a transaction is active
// or not. This allows detection of unauthorized flow
// (pulses without an active transaction).
//
// USAGE:
//
//   #include "sampler/sampler.h"
//
//   void setup() {
//       sampler_init();
//   }
//
//   // In your processing task (via scheduler):
//   static uint32_t cursor = 0;
//   SamplerEvent evt;
//   while (sampler_read_next(cursor, evt)) {
//       // process evt.state (8-bit: A1-A4 = bit 0-3, B1-B4 = bit 4-7)
//   }
//
// =======================================================
#include <Arduino.h>
#include "board/pins.h"

// Event from sampler: when and what state
struct SamplerEvent {
    uint32_t sample_index;  // running sample counter
    uint8_t  state;         // bit0=A1, bit1=A2, bit2=A3, bit3=A4
                            // bit4=B1, bit5=B2, bit6=B3, bit7=B4
};

// Initialize sampler (sets up hardware timer + ISR)
// Runs at 4 kHz (250 µs interval), always on.
void sampler_init();

// Read next event from ring buffer.
// cursor: your own read position (you own this variable)
// out: filled with next event if available
// Returns true if there was a new event, false if caught up.
bool sampler_read_next(uint32_t &cursor, SamplerEvent &out);

// Status helpers
bool sampler_ready();
uint8_t sampler_current_state();
uint32_t sampler_write_position();
uint32_t sampler_sample_index();
size_t sampler_buffer_size();
