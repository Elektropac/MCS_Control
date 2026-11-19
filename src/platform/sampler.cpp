#include "sampler.h"
#include "pins.h"
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

static const uint32_t SAMPLER_INTERVAL_US = 250;
static const size_t   SAMPLER_BUFFER_SIZE = 4096;

// Ring buffer, stored as two parallel arrays (indices + states) instead of an
// array of SamplerEvent structs: the struct is 8 bytes (4-byte index + 1-byte
// state + 3 bytes padding), the split layout is 5 bytes per entry. Same
// capacity (4096 events), 12 KB less internal RAM.
static volatile uint32_t s_ring_index[SAMPLER_BUFFER_SIZE];
static volatile uint8_t  s_ring_state[SAMPLER_BUFFER_SIZE];
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

// Read all 8 inputs with TWO register reads (GPIO_IN_REG = pins 0-31,
// GPIO_IN1_REG = pins 32-53) instead of 8 separate gpio_get_level() calls.
// This runs in the 4 kHz timer ISR, so the saving is paid 4000x per second.
// The pins are compile-time constants, so the shifts/masks fold away.
static constexpr inline __attribute__((always_inline)) uint32_t pin_bit(int pin, uint32_t lo, uint32_t hi) {
    return pin < 32 ? ((lo >> pin) & 1u) : ((hi >> (pin - 32)) & 1u);
}

static inline uint8_t IRAM_ATTR read_signals() {
    const uint32_t lo = REG_READ(GPIO_IN_REG);
    const uint32_t hi = REG_READ(GPIO_IN1_REG);
    return (uint8_t)(
        (pin_bit(A1_DIGITAL, lo, hi) << 0) |
        (pin_bit(A2_DIGITAL, lo, hi) << 1) |
        (pin_bit(A3_DIGITAL, lo, hi) << 2) |
        (pin_bit(A4_DIGITAL, lo, hi) << 3) |
        (pin_bit(B1_DIGITAL, lo, hi) << 4) |
        (pin_bit(B2_DIGITAL, lo, hi) << 5) |
        (pin_bit(B3_DIGITAL, lo, hi) << 6) |
        (pin_bit(B4_DIGITAL, lo, hi) << 7));
}

static void IRAM_ATTR sample_isr() {
    uint8_t s = read_signals();
    if (s != s_last_state) {
        uint32_t pos = s_write_pos % SAMPLER_BUFFER_SIZE;
        s_ring_index[pos] = s_sample_index;
        s_ring_state[pos] = s;
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

    // Timer setup — arduino-esp32 3.x API. timerBegin takes a frequency in
    // Hz directly: 1 MHz gives a 1 µs tick, so the alarm value is simply the
    // interval in microseconds. (The old 2.x API used prescaler + count.)
    s_timer = timerBegin(1000000);                       // 1 MHz -> 1 µs ticks
    timerAttachInterrupt(s_timer, &sample_isr);          // no edge arg in 3.x
    timerAlarm(s_timer, SAMPLER_INTERVAL_US, true, 0);   // period, autoreload, unlimited

    s_ready = true;
}

bool sampler_read_next(uint32_t &cursor, SamplerEvent &out) {
    uint32_t w = s_write_pos;
    if (cursor == w) return false;

    uint32_t pos = cursor % SAMPLER_BUFFER_SIZE;
    out.sample_index = s_ring_index[pos];
    out.state        = s_ring_state[pos];
    cursor++;
    return true;
}

bool sampler_ready() { return s_ready; }
uint8_t sampler_current_state() { return s_last_state; }
uint32_t sampler_write_position() { return s_write_pos; }
uint32_t sampler_sample_index() { return s_sample_index; }
size_t sampler_buffer_size() { return SAMPLER_BUFFER_SIZE; }
