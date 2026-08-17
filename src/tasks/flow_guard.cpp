#include "flow_guard.h"
#include "sampler.h"

static const uint8_t  NOISE_THRESHOLD   = 3;
static const uint32_t CHECK_INTERVAL_MS = 500;

static uint32_t s_cursor = 0;
static uint8_t  s_last_state = 0;
static uint32_t s_pulse_count = 0;
static TaskHandle_t s_task_handle = nullptr;

static uint32_t count_edges(uint8_t old_state, uint8_t new_state) {
    uint8_t changed = old_state ^ new_state;
    uint32_t edges = 0;
    for (int i = 0; i < 8; i++) {
        if (changed & (1 << i)) edges++;
    }
    return edges;
}

static void flow_guard_task(void* param) {
    (void)param;
    for (;;) {
        SamplerEvent evt;
        while (sampler_read_next(s_cursor, evt)) {
            uint32_t edges = count_edges(s_last_state, evt.state);
            s_pulse_count += edges;
            s_last_state = evt.state;
        }

        if (s_pulse_count > NOISE_THRESHOLD) {
            // TODO: send alarm event to interface
            s_pulse_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

void flow_guard_init() {
    s_cursor = sampler_write_position();
    s_last_state = sampler_current_state();
    s_pulse_count = 0;
}

void flow_guard_start_task() {
    xTaskCreate(flow_guard_task, "flow_guard", 2048, nullptr, 1, &s_task_handle);
}

void flow_guard_pause() {
    if (s_task_handle) vTaskSuspend(s_task_handle);
}

void flow_guard_resume() {
    s_cursor = sampler_write_position();
    s_last_state = sampler_current_state();
    s_pulse_count = 0;
    if (s_task_handle) vTaskResume(s_task_handle);
}
