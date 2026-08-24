#include "sampler.h"
#include "pins.h"
#include <driver/gpio.h>

static const uint32_t SAMPLER_INTERVAL_US = 200;
static const size_t   SAMPLER_BUFFER_SIZE = 4096;

static volatile SamplerEvent s_ring[SAMPLER_BUFFER_SIZE];
static volatile uint32_t     s_write_pos    = 0;
static volatile uint32_t     s_sample_index = 0;
static volatile uint8_t      s_last_state   = 0;
static volatile bool         s_ready        = false;

static hw_timer_t *s_timer = nullptr;

static const int SAMPLER_PINS[8] = {
    A1_DIGITAL, A2_DIGITAL, A3_DIGITAL, A4_DIGITAL,
    B1_DIGITAL, B2_DIGITAL, B3_DIGITAL, B4_DIGITAL
};

static void configure_pullups() {
    for (int i = 0; i < 8; i++) {
        pinMode(SAMPLER_PINS[i], INPUT_PULLUP);
        gpio_set_pull_mode((gpio_num_t)SAMPLER_PINS[i], GPIO_PULLUP_ONLY);
    }
}

static inline uint8_t IRAM_ATTR read_signals() {
    uint8_t s = 0;
    s |= (gpio_get_level((gpio_num_t)A1_DIGITAL) << 0);
    s |= (gpio_get_level((gpio_num_t)A2_DIGITAL) << 1);
    s |= (gpio_get_level((gpio_num_t)A3_DIGITAL) << 2);
    s |= (gpio_get_level((gpio_num_t)A4_DIGITAL) << 3);
    s |= (gpio_get_level((gpio_num_t)B1_DIGITAL) << 4);
    s |= (gpio_get_level((gpio_num_t)B2_DIGITAL) << 5);
    s |= (gpio_get_level((gpio_num_t)B3_DIGITAL) << 6);
    s |= (gpio_get_level((gpio_num_t)B4_DIGITAL) << 7);
    return s;
}

static void IRAM_ATTR sample_isr() {
    uint8_t s = read_signals();
    if (s != s_last_state) {
        uint32_t pos = s_write_pos % SAMPLER_BUFFER_SIZE;
        s_ring[pos].sample_index = s_sample_index;
        s_ring[pos].state        = s;
        s_write_pos++;
        s_last_state = s;
    }
    s_sample_index++;
}

void sampler_init() {
    s_ready = false;
    configure_pullups();

    s_write_pos    = 0;
    s_sample_index = 0;
    s_last_state   = read_signals();

    s_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(s_timer, &sample_isr, true);
    timerAlarmWrite(s_timer, SAMPLER_INTERVAL_US, true);
    timerAlarmEnable(s_timer);

    s_ready = true;
}

bool sampler_read_next(uint32_t &cursor, SamplerEvent &out) {
    uint32_t w = s_write_pos;
    if (cursor == w) return false;

    uint32_t pos = cursor % SAMPLER_BUFFER_SIZE;
    out.sample_index = s_ring[pos].sample_index;
    out.state        = s_ring[pos].state;
    cursor++;
    return true;
}

bool sampler_ready() { return s_ready; }
uint8_t sampler_current_state() { return s_last_state; }
uint32_t sampler_write_position() { return s_write_pos; }
uint32_t sampler_sample_index() { return s_sample_index; }
size_t sampler_buffer_size() { return SAMPLER_BUFFER_SIZE; }
