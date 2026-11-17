#pragma once
// =======================================================
// CPU LOAD — per-core load measured with the FreeRTOS idle hook
// =======================================================
// The idle task on each core calls a tiny hook in a tight loop. We timestamp
// every call; when two calls are close together (< IDLE_GAP_US) the core was
// idle in between, so that time is added to the idle counter. Anything longer
// means a task (or interrupt) ran, i.e. the core was busy.
//
// Cost: a few hundred nanoseconds per idle loop iteration — it only runs
// while the core has nothing else to do, so it never takes CPU from real work.
#include <Arduino.h>

void cpu_load_init();

// Load in percent (0..100) per core since the previous call to this function
// (or since boot for the first call). Resets the measurement window.
void cpu_load_sample(uint8_t &core0_pct, uint8_t &core1_pct, uint32_t &window_ms);
