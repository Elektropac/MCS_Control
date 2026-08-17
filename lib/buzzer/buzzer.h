#pragma once
// =======================================================
// BUZZER — piezo buzzer with tone/melody support
// =======================================================
// Uses ESP32 LEDC PWM for tone generation.
// Melodies are arrays of {frequency, duration} pairs.
// A FreeRTOS task handles non-blocking melody playback.
//
// Pin specified at init. No board dependencies.
//
// Usage:
//   buzzer_init(21);           // GPIO pin
//   buzzer_beep(2400, 100);   // 2400 Hz for 100ms
//   buzzer_play(melody, 5);   // play melody array
//
// =======================================================
#include <Arduino.h>

// A single note: frequency in Hz (0 = silence), duration in ms
struct Note {
    uint16_t freq_hz;
    uint16_t duration_ms;
};

// Initialize buzzer on given pin + start melody task
void buzzer_init(uint8_t pin);

// Play a single tone (0 = stop)
void buzzer_tone(uint16_t freq_hz);

// Stop buzzer
void buzzer_stop();

// Simple beep: frequency for duration_ms, then stop
void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms);

// Play a melody (array of notes). Non-blocking.
void buzzer_play(const Note* melody, uint8_t num_notes);

// Is a melody currently playing?
bool buzzer_is_playing();

// --- Built-in sounds ---
void buzzer_sound_ok();
void buzzer_sound_error();
void buzzer_sound_startup();
void buzzer_sound_click();
void buzzer_sound_warning();
void buzzer_sound_complete();
