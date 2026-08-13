#pragma once
// =======================================================
// BUZZER — piezo buzzer with tone/melody support
// =======================================================
//
// Pin: PIN_BUZZER (GPIO 21)
// Uses PWM (ledc) for tone generation.
// Melodies are arrays of {frequency, duration} pairs.
// A scheduler task advances through the notes.
//
// =======================================================
#include <Arduino.h>

// A single note: frequency in Hz (0 = silence), duration in ms
struct Note {
    uint16_t freq_hz;
    uint16_t duration_ms;
};

// Initialize buzzer
void buzzer_init();

// Play a single tone (0 = stop)
void buzzer_tone(uint16_t freq_hz);

// Stop buzzer
void buzzer_stop();

// Simple beep: frequency for duration_ms, then stop
void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms);

// Play a melody (array of notes). Non-blocking, uses scheduler.
void buzzer_play(const Note* melody, uint8_t num_notes);

// Is a melody currently playing?
bool buzzer_is_playing();

// --- Built-in sounds ---
void buzzer_sound_ok();         // short confirmation beep
void buzzer_sound_error();      // error/alarm sound
void buzzer_sound_startup();    // boot jingle
void buzzer_sound_click();      // button click feedback
void buzzer_sound_warning();    // attention needed
void buzzer_sound_complete();   // transaction/task done
