#include "buzzer.h"

// ------------------------------------------
// PWM config (ESP32 LEDC)
// ------------------------------------------
#define BUZZER_CHANNEL    0
#define BUZZER_RESOLUTION 8

// ------------------------------------------
// State
// ------------------------------------------
static uint8_t s_pin = 0;
static const Note* s_melody = nullptr;
static uint8_t s_melody_len = 0;
static uint8_t s_melody_pos = 0;
static unsigned long s_note_start = 0;
static bool s_playing = false;
static TaskHandle_t s_buzzer_task_handle = nullptr;

// ------------------------------------------
// Low-level tone control
// ------------------------------------------
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
// Init + FreeRTOS task
// ------------------------------------------
void buzzer_init(uint8_t pin) {
    s_pin = pin;

    ledcSetup(BUZZER_CHANNEL, 1000, BUZZER_RESOLUTION);
    ledcAttachPin(s_pin, BUZZER_CHANNEL);
    ledcWrite(BUZZER_CHANNEL, 0);

    // Create melody task (waits for notification)
    xTaskCreate(
        [](void* param) {
            (void)param;
            for (;;) {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                while (s_playing) {
                    unsigned long now = millis();
                    unsigned long elapsed = now - s_note_start;

                    if (elapsed >= s_melody[s_melody_pos].duration_ms) {
                        s_melody_pos++;

                        if (s_melody_pos >= s_melody_len) {
                            buzzer_stop();
                            break;
                        }

                        s_note_start = now;
                        buzzer_tone(s_melody[s_melody_pos].freq_hz);
                    }

                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        },
        "buzzer",
        2048,
        nullptr,
        1,
        &s_buzzer_task_handle
    );
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

    buzzer_tone(melody[0].freq_hz);

    if (s_buzzer_task_handle != nullptr) {
        xTaskNotifyGive(s_buzzer_task_handle);
    }
}

void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms) {
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

static const Note SND_OK[] = { {2400, 80} };
void buzzer_sound_ok() { buzzer_play(SND_OK, 1); }

static const Note SND_ERROR[] = {
    {1500, 150}, {0, 30}, {1000, 150}, {0, 30}, {600, 250}
};
void buzzer_sound_error() { buzzer_play(SND_ERROR, 5); }

static const Note SND_STARTUP[] = {
    {800, 100}, {0, 20}, {1200, 100}, {0, 20}, {1600, 100}, {0, 20}, {2400, 150}
};
void buzzer_sound_startup() { buzzer_play(SND_STARTUP, 7); }

static const Note SND_CLICK[] = { {4000, 15} };
void buzzer_sound_click() { buzzer_play(SND_CLICK, 1); }

static const Note SND_WARNING[] = {
    {2000, 100}, {0, 50}, {2000, 100}, {0, 50}, {2000, 100}
};
void buzzer_sound_warning() { buzzer_play(SND_WARNING, 5); }

static const Note SND_COMPLETE[] = {
    {1200, 120}, {0, 30}, {1800, 200}
};
void buzzer_sound_complete() { buzzer_play(SND_COMPLETE, 3); }
