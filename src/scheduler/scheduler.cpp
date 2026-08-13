#include "scheduler.h"

static Task tasks[MAX_TASKS];
static int num_tasks = 0;

int task_add(const char* name, TaskFunction func, unsigned long interval_ms,
             TaskPriority priority, bool one_shot) {
    if (num_tasks >= MAX_TASKS) return -1;
    if (func == nullptr) return -1;

    tasks[num_tasks].name            = name;
    tasks[num_tasks].func            = func;
    tasks[num_tasks].interval_ms     = interval_ms;
    tasks[num_tasks].last_run        = 0;
    tasks[num_tasks].enabled         = true;
    tasks[num_tasks].one_shot        = one_shot;
    tasks[num_tasks].priority        = priority;
    tasks[num_tasks].last_runtime_us = 0;
    tasks[num_tasks].max_runtime_us  = 0;

    return num_tasks++;
}

void task_run() {
    unsigned long now = millis();

    // Run in priority order: HIGH first, then NORMAL, then LOW
    for (int p = PRIORITY_HIGH; p <= PRIORITY_LOW; p++) {
        for (int i = 0; i < num_tasks; i++) {
            if (!tasks[i].enabled) continue;
            if (tasks[i].priority != p) continue;
            if (now - tasks[i].last_run < tasks[i].interval_ms) continue;

            tasks[i].last_run = now;

            // Measure execution time
            unsigned long start = micros();
            tasks[i].func();
            unsigned long elapsed = micros() - start;

            tasks[i].last_runtime_us = elapsed;
            if (elapsed > tasks[i].max_runtime_us) {
                tasks[i].max_runtime_us = elapsed;
            }

            // One-shot: remove after execution
            if (tasks[i].one_shot) {
                task_remove(tasks[i].func);
                i--;  // array shifted, re-check this index
            }
        }
    }
}

static int find_task(TaskFunction func) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].func == func) return i;
    }
    return -1;
}

void task_remove(TaskFunction func) {
    int id = find_task(func);
    if (id < 0) return;

    num_tasks--;
    if (id < num_tasks) {
        tasks[id] = tasks[num_tasks];
    }
}

void task_enable(TaskFunction func, bool state) {
    int id = find_task(func);
    if (id >= 0) tasks[id].enabled = state;
}

void task_set_interval(TaskFunction func, unsigned long interval_ms) {
    int id = find_task(func);
    if (id >= 0) tasks[id].interval_ms = interval_ms;
}

bool task_is_enabled(TaskFunction func) {
    int id = find_task(func);
    if (id >= 0) return tasks[id].enabled;
    return false;
}

int task_count() {
    return num_tasks;
}

unsigned long task_get_last_runtime(TaskFunction func) {
    int id = find_task(func);
    if (id >= 0) return tasks[id].last_runtime_us;
    return 0;
}

unsigned long task_get_max_runtime(TaskFunction func) {
    int id = find_task(func);
    if (id >= 0) return tasks[id].max_runtime_us;
    return 0;
}
