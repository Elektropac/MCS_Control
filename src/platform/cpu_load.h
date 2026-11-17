#pragma once
// =======================================================
// CPU LOAD — per-core load from FreeRTOS run-time statistics
// =======================================================
// The prebuilt SDK has CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS enabled, so the
// two IDLE tasks report how long they ran. Load of a core = 1 - idle/wall.
// Interrupt time that interrupts the idle task is counted as idle by FreeRTOS,
// so the 4 kHz sampler ISR is NOT included — the number is "task load", good
// for comparing builds, slightly below the true total.
#include <Arduino.h>

void cpu_load_init();   // starts the first measurement window

// Load in percent (0..100) per core since the previous call to this function
// (or since cpu_load_init for the first call). Resets the window.
void cpu_load_sample(uint8_t &core0_pct, uint8_t &core1_pct, uint32_t &window_ms);
