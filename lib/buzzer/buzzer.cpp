#include "buzzer.h"
#include "soc/gpio_struct.h"

// ------------------------------------------
// State
// ------------------------------------------
static uint8_t s_pin = 0;
static bool s_playing = false;
static volatile bool s_tone_active = false;
static volatile bool s_pin_state = false;
static volatile uint32_t s_half_period_us = 500;  // default 1kHz
static hw_timer_t* s_timer = nullptr;
static volatile uint32_t s_tick_count = 0;

// ------------------------------------------
// Timer ISR — runs at fixed 10µs, counts up to half_period
// ------------------------------------------
static void IRAM_ATTR onTimer() {
    s_tick_count++;
    if (s_tick_count >= s_half_period_us) {
        s_tick_count = 0;
        if (s_tone_active) {
            s_pin_state = !s_pin_state;
            if (s_pin_state) {
                GPIO.out_w1ts = (1UL << s_pin);
            } else {
                GPIO.out_w1tc = (1UL << s_pin);
            }
        } else {
            GPIO.out_w1tc = (1UL << s_pin);
            s_pin_state = false;
        }
    }
}

// ------------------------------------------
// Low-level tone control
// ------------------------------------------
void buzzer_tone(uint16_t freq_hz) {
    if (freq_hz == 0) {
        s_tone_active = false;
    } else {
        // half_period in units of 10µs
        s_half_period_us = 50000UL / freq_hz;
        if (s_half_period_us < 1) s_half_period_us = 1;
        s_tick_count = 0;
        s_tone_active = true;
    }
}

void buzzer_stop() {
    s_tone_active = false;
    GPIO.out_w1tc = (1UL << s_pin);
    s_pin_state = false;
    s_playing = false;
}

// ------------------------------------------
// Init
// ------------------------------------------
void buzzer_init(uint8_t pin) {
    s_pin = pin;
    pinMode(s_pin, OUTPUT);
    digitalWrite(s_pin, LOW);

    // Timer 1, prescaler 80 = 1µs per tick, auto-reload
    s_timer = timerBegin(1, 80, true);
    timerAttachInterrupt(s_timer, &onTimer, true);
    // Fire every 10µs (100kHz base rate)
    timerAlarmWrite(s_timer, 10, true);
    timerAlarmEnable(s_timer);
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
