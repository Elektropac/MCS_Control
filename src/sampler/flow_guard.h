#pragma once
// =======================================================
// FLOW GUARD — detects unauthorized flow (pulses without
// an active transaction)
// =======================================================
//
// HOW IT WORKS:
// A scheduler task runs when no transaction is active.
// It reads from the sampler ring buffer and counts pulses.
// If pulses exceed a noise threshold, it raises an alarm
// via the interface layer (event to Niklas).
//
// When a transaction starts → disable this task.
// When a transaction ends → reset and re-enable.
//
// =======================================================
#include <Arduino.h>

// Initialize flow guard state (call once in setup, before task_add)
void flow_guard_init();

// Task function — register this with task_add() in setup
void flow_guard_check();

// Call when a transaction starts — pauses monitoring
void flow_guard_pause();

// Call when a transaction ends — resets and resumes monitoring
void flow_guard_resume();
