#include "buzzer.h"
#include "board/pins.h"
#include "scheduler/scheduler.h"

// ------------------------------------------
// PWM config (ESP32 LEDC)
// ------------------------------------------
#define BUZZER_CHANNEL  0
#define BUZZER_RESOLUTION 8

// ------------------------------------------
// Melody playback state
// ------------------------------------------
static const Note* s_melody = nullptr;
static uint8_t s_melody_len = 0;
static uint8_t s_melody_pos = 0;
static unsigned long s_note_start = 0;
static bool s_playing = false;

// ------------------------------------------
// Low-level tone control
// ------------------------------------------
void buzzer_init() {
    ledcSetup(BUZZER_CHANNEL, 1000, BUZZER_RESOLUTION);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
    ledcWrite(BUZZER_CHANNEL, 0);
}

void buzzer_tone(uint16_t freq_hz) {
    if (freq_hz == 0) {
        ledcWrite(BUZZER_CHANNEL, 0);
    } else {
        ledcWriteTone(BUZZER_CHANNEL, freq_hz);
        ledcWrite(BUZZER_CHANNEL, 128);  // 50% duty
    }
}

void buzzer_stop() {
    ledcWrite(BUZZER_CHANNEL, 0);
    s_playing = false;
}

// ------------------------------------------
// Melody task — advances through notes
// ------------------------------------------
static void buzzer_melody_task() {
    if (!s_playing) return;

    unsigned long now = millis();
    unsigned long elapsed = now - s_note_start;

    // Current note finished?
    if (elapsed >= s_melody[s_melody_pos].duration_ms) {
        s_melody_pos++;

        if (s_melody_pos >= s_melody_len) {
            // Melody done
            buzzer_stop();
            task_enable(buzzer_melody_task, false);
            return;
        }

        // Start next note
        s_note_start = now;
        buzzer_tone(s_melody[s_melody_pos].freq_hz);
    }
}

// ------------------------------------------
// Play melody (non-blocking)
// ------------------------------------------
void buzzer_play(const Note* melody, uint8_t num_notes) {
    if (num_notes == 0 || melody == nullptr) return;

    s_melody = melody;
    s_melody_len = num_notes;
    s_melody_pos = 0;
    s_note_start = millis();
    s_playing = true;

    // Start first note
    buzzer_tone(melody[0].freq_hz);

    // Enable melody task (register once, re-enable on each play)
    static bool registered = false;
    if (!registered) {
        task_add("buzzer_melody", buzzer_melody_task, 10, PRIORITY_LOW);
        registered = true;
    } else {
        task_enable(buzzer_melody_task, true);
    }
}

void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms) {
    static const Note single[2] = {{0, 0}, {0, 0}};
    // Hack: just use play with 1 note + silence
    static Note beep_note[2];
    beep_note[0] = {freq_hz, duration_ms};
    beep_note[1] = {0, 10};
    buzzer_play(beep_note, 2);
}

bool buzzer_is_playing() {
    return s_playing;
}

// ------------------------------------------
// Built-in sounds
// ------------------------------------------

// Short confirmation: single high beep
static const Note SND_OK[] = {
    {2400, 80},
};

void buzzer_sound_ok() {
    buzzer_play(SND_OK, 1);
}

// Error: three descending tones
static const Note SND_ERROR[] = {
    {1500, 150},
    {0, 30},
    {1000, 150},
    {0, 30},
    {600, 250},
};

void buzzer_sound_error() {
    buzzer_play(SND_ERROR, 5);
}

// Startup: ascending arpeggio
static const Note SND_STARTUP[] = {
    {800, 100},
    {0, 20},
    {1200, 100},
    {0, 20},
    {1600, 100},
    {0, 20},
    {2400, 150},
};

void buzzer_sound_startup() {
    buzzer_play(SND_STARTUP, 7);
}

// Click: very short tick
static const Note SND_CLICK[] = {
    {4000, 15},
};

void buzzer_sound_click() {
    buzzer_play(SND_CLICK, 1);
}

// Warning: two-tone alternating
static const Note SND_WARNING[] = {
    {2000, 100},
    {0, 50},
    {2000, 100},
    {0, 50},
    {2000, 100},
};

void buzzer_sound_warning() {
    buzzer_play(SND_WARNING, 5);
}

// Complete: happy two-note chime
static const Note SND_COMPLETE[] = {
    {1200, 120},
    {0, 30},
    {1800, 200},
};

void buzzer_sound_complete() {
    buzzer_play(SND_COMPLETE, 3);
}
