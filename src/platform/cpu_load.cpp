#include "cpu_load.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

static int64_t  s_win_start_us = 0;
static uint32_t s_idle0_prev = 0;
static uint32_t s_idle1_prev = 0;
static bool     s_have_prev = false;

// Fetch the accumulated run time (us) of IDLE0 / IDLE1.
static bool read_idle(uint32_t &idle0, uint32_t &idle1) {
    UBaseType_t cap = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *st = (TaskStatus_t *)malloc(cap * sizeof(TaskStatus_t));
    if (!st) return false;
    uint32_t total = 0;
    UBaseType_t n = uxTaskGetSystemState(st, cap, &total);
    idle0 = idle1 = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        if (strcmp(st[i].pcTaskName, "IDLE0") == 0) idle0 = st[i].ulRunTimeCounter;
        else if (strcmp(st[i].pcTaskName, "IDLE1") == 0) idle1 = st[i].ulRunTimeCounter;
    }
    free(st);
    return true;
}

void cpu_load_init() {
    s_win_start_us = esp_timer_get_time();
    s_have_prev = read_idle(s_idle0_prev, s_idle1_prev);
}

void cpu_load_sample(uint8_t &core0_pct, uint8_t &core1_pct, uint32_t &window_ms) {
    uint32_t i0, i1;
    int64_t now = esp_timer_get_time();
    int64_t span = now - s_win_start_us;
    core0_pct = core1_pct = 0;
    window_ms = (uint32_t)(span > 0 ? span / 1000 : 0);
    if (!read_idle(i0, i1) || !s_have_prev || span <= 0) {
        s_idle0_prev = i0; s_idle1_prev = i1; s_have_prev = true; s_win_start_us = now;
        return;
    }
    int64_t d0 = (int64_t)(uint32_t)(i0 - s_idle0_prev);
    int64_t d1 = (int64_t)(uint32_t)(i1 - s_idle1_prev);
    int64_t b0 = span - d0; if (b0 < 0) b0 = 0; if (b0 > span) b0 = span;
    int64_t b1 = span - d1; if (b1 < 0) b1 = 0; if (b1 > span) b1 = span;
    core0_pct = (uint8_t)((b0 * 100) / span);
    core1_pct = (uint8_t)((b1 * 100) / span);
    s_idle0_prev = i0; s_idle1_prev = i1; s_win_start_us = now;
}
