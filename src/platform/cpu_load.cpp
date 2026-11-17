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

struct TaskPrev { UBaseType_t num; uint32_t rt; };
static TaskPrev s_tp[48];
static uint8_t  s_tp_n = 0;
static int64_t  s_tp_start_us = 0;

void cpu_load_print_tasks() {
    UBaseType_t cap = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *st = (TaskStatus_t *)malloc(cap * sizeof(TaskStatus_t));
    if (!st) { Serial.println("no mem"); return; }
    uint32_t total = 0;
    UBaseType_t n = uxTaskGetSystemState(st, cap, &total);
    int64_t now = esp_timer_get_time();
    int64_t span = now - s_tp_start_us;
    if (span <= 0) span = 1;
    Serial.printf("TASK CPU over %lu ms (name core %%)\n", (unsigned long)(span / 1000));
    TaskPrev next[48]; uint8_t nn = 0;
    for (UBaseType_t i = 0; i < n && nn < 48; i++) {
        uint32_t prev = 0;
        for (uint8_t j = 0; j < s_tp_n; j++) if (s_tp[j].num == st[i].xTaskNumber) { prev = s_tp[j].rt; break; }
        uint32_t d = st[i].ulRunTimeCounter - prev;
        next[nn].num = st[i].xTaskNumber; next[nn].rt = st[i].ulRunTimeCounter; nn++;
        int pct10 = (int)(((int64_t)d * 1000) / span);       // tenths of a percent
        if (pct10 >= 5)
            Serial.printf("  %-14s core%d %d.%d%%\n", st[i].pcTaskName,
                          (int)st[i].xCoreID, pct10 / 10, pct10 % 10);
    }
    memcpy(s_tp, next, nn * sizeof(TaskPrev));
    s_tp_n = nn; s_tp_start_us = now;
    free(st);
}
