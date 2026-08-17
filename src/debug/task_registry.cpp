#include "task_registry.h"
#include <string.h>

static RegisteredTask s_tasks[MAX_REGISTERED_TASKS];
static int s_count = 0;

void task_register(TaskHandle_t handle, const char* name, UBaseType_t priority, uint32_t stack_size) {
    if (s_count >= MAX_REGISTERED_TASKS) return;
    s_tasks[s_count].handle     = handle;
    s_tasks[s_count].name       = name;
    s_tasks[s_count].priority   = priority;
    s_tasks[s_count].stack_size = stack_size;
    s_count++;
}

int task_registry_count() { return s_count; }

const RegisteredTask* task_registry_get(int index) {
    if (index < 0 || index >= s_count) return nullptr;
    return &s_tasks[index];
}

TaskHandle_t task_registry_find(const char* name) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_tasks[i].name, name) == 0) return s_tasks[i].handle;
    }
    return nullptr;
}

bool task_registry_suspend(const char* name) {
    TaskHandle_t h = task_registry_find(name);
    if (!h) return false;
    vTaskSuspend(h);
    return true;
}

bool task_registry_resume(const char* name) {
    TaskHandle_t h = task_registry_find(name);
    if (!h) return false;
    vTaskResume(h);
    return true;
}
