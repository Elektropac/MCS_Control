#include "buzzer.h"

// ------------------------------------------
// Tone generation: LEDC hardware PWM (no ISR — clean tone even while the
// 4 kHz sampler ISR runs). Pin 21 is the buzzer's alone (the old NeoPixel
// RGB that also claimed pin 21 has been removed).
// ledcWriteTone() sets frequency AND a 50% duty itself.
// ------------------------------------------
#define BUZZER_LEDC_CHANNEL   0

static uint8_t s_pin = 0;
static bool s_playing = false;

void buzzer_tone(uint16_t freq_hz) {
    if (freq_hz == 0) {
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    } else {
        ledcWriteTone(BUZZER_LEDC_CHANNEL, freq_hz);
    }
}

void buzzer_stop() {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    s_playing = false;
}

void buzzer_init(uint8_t pin) {
    s_pin = pin;
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 10);
    ledcAttachPin(s_pin, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

// ------------------------------------------
// Play melody (blocking)
// ------------------------------------------
void buzzer_play(const Note* melody, uint8_t num_notes) {
    if (num_notes == 0 || melody == nullptr) return;

    s_playing = true;

    for (uint8_t i = 0; i < num_notes; i++) {
        buzzer_tone(melody[i].freq_hz);
        vTaskDelay(pdMS_TO_TICKS(melody[i].duration_ms));
    }

    buzzer_stop();
}

void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms) {
    buzzer_tone(freq_hz);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_stop();
}

bool buzzer_is_playing() {
    return s_playing;
}

// ------------------------------------------
// Built-in sounds
// ------------------------------------------

static const Note SND_OK[] = { {1200, 150}, {0, 20} };
void buzzer_sound_ok() { buzzer_play(SND_OK, 2); }

static const Note SND_ERROR[] = {
    {1000, 200}, {0, 50}, {800, 200}, {0, 50}, {600, 300}, {0, 20}
};
void buzzer_sound_error() { buzzer_play(SND_ERROR, 6); }

static const Note SND_STARTUP[] = {
    {800, 100}, {0, 20}, {1000, 100}, {0, 20}, {1200, 100}, {0, 20}, {1600, 150}, {0, 10}
};
void buzzer_sound_startup() { buzzer_play(SND_STARTUP, 8); }

static const Note SND_CLICK[] = { {1000, 150}, {0, 20} };
void buzzer_sound_click() { buzzer_play(SND_CLICK, 2); }

static const Note SND_WARNING[] = {
    {800, 150}, {0, 80}, {800, 150}, {0, 80}, {800, 150}, {0, 20}
};
void buzzer_sound_warning() { buzzer_play(SND_WARNING, 6); }

static const Note SND_COMPLETE[] = {
    {800, 150}, {0, 50}, {1000, 150}, {0, 50}, {1200, 200}, {0, 20}
};
void buzzer_sound_complete() { buzzer_play(SND_COMPLETE, 6); }
