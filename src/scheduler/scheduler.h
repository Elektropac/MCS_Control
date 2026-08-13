#pragma once
// =======================================================
// SCHEDULER — cooperative task scheduler for MCU
// =======================================================
//
// WHY THIS EXISTS:
// On a computer, the OS handles multitasking (threads, async).
// On an MCU there is no OS — just one loop() running forever.
// If any task blocks (e.g. delay()), everything else stops:
// no pulse counting, no button reading, no display update.
//
// The scheduler solves this: each task is registered with an
// interval. loop() calls task_run() continuously, and the
// scheduler checks "is it time for this task?" — if yes, run
// it. Nothing blocks, everything gets its turn.
//
// Think of it as a simple round-robin without an OS.
//
// USAGE:
//
//   #include "scheduler/scheduler.h"
//
//   void read_sensors() { /* ... */ }
//   void check_buttons() { /* ... */ }
//
//   void setup() {
//       task_add("sensors", read_sensors, 1000);  // every 1s
//       task_add("buttons", check_buttons, 50);   // every 50ms
//       task_add("timeout", my_timeout, 5000, PRIORITY_LOW, true);  // one-shot
//   }
//
//   void loop() {
//       task_run();  // this is all you need here
//   }
//
// AVAILABLE FUNCTIONS:
//
//   task_add(name, func, interval_ms, priority, one_shot)
//       Register a new task. Returns task id or -1 if full.
//       priority defaults to PRIORITY_NORMAL.
//       one_shot defaults to false.
//       Can be called at any time, not just in setup().
//
//   task_run()
//       Call once per loop(). Executes all tasks that are due.
//       Tasks run in priority order (HIGH before NORMAL before LOW).
//
//   task_enable(func, true/false)
//       Enable or disable a task without removing it.
//
//   task_set_interval(func, interval_ms)
//       Change a task's interval at runtime (e.g. from config).
//
//   task_remove(func)
//       Remove a task completely, freeing the slot.
//
//   task_is_enabled(func)
//       Returns true if the task is currently enabled.
//
//   task_count()
//       Returns number of registered tasks.
//
//   task_get_max_runtime(func)
//       Returns the longest execution time (in µs) recorded
//       for this task. Useful for detecting blockers.
//
//   task_get_last_runtime(func)
//       Returns the last execution time (in µs) for this task.
//
// RULES:
//   - Never use delay() inside a task. It blocks everything.
//   - Keep tasks short. If something takes long, split it up.
//   - Max 20 tasks (increase MAX_TASKS if needed).
//
// =======================================================
#include <Arduino.h>

#define MAX_TASKS 20

// Task priority levels — higher runs first
enum TaskPriority {
    PRIORITY_HIGH   = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_LOW    = 2,
};

typedef void (*TaskFunction)();

struct Task {
    TaskFunction    func;
    unsigned long   interval_ms;
    unsigned long   last_run;
    bool            enabled;
    bool            one_shot;       // run once then auto-remove
    TaskPriority    priority;
    const char*     name;
    unsigned long   last_runtime_us;  // last execution duration
    unsigned long   max_runtime_us;   // worst case execution duration
};

// Tilføj en task. Returnerer task-id eller -1 ved fejl.
int task_add(const char* name, TaskFunction func, unsigned long interval_ms,
             TaskPriority priority = PRIORITY_NORMAL, bool one_shot = false);

// Kør alle tasks der er klar. Kald denne i loop().
void task_run();

// Fjern en task helt (frigør pladsen)
void task_remove(TaskFunction func);

// Enable/disable en task
void task_enable(TaskFunction func, bool state);

// Ændr interval runtime (fx fra config)
void task_set_interval(TaskFunction func, unsigned long interval_ms);

// Tjek om en task er enabled
bool task_is_enabled(TaskFunction func);

// Hent antal registrerede tasks
int task_count();

// Timing diagnostik
unsigned long task_get_last_runtime(TaskFunction func);
unsigned long task_get_max_runtime(TaskFunction func);
