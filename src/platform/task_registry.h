#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MAX_REGISTERED_TASKS 10

struct RegisteredTask {
    TaskHandle_t handle;
    const char*  name;
    UBaseType_t  priority;
    uint32_t     stack_size;
};

void task_register(TaskHandle_t handle, const char* name, UBaseType_t priority, uint32_t stack_size);
// Remove a task (call before it deletes itself, so no stale handle is left behind).
void task_unregister(TaskHandle_t handle);
int task_registry_count();
const RegisteredTask* task_registry_get(int index);
TaskHandle_t task_registry_find(const char* name);
bool task_registry_suspend(const char* name);
bool task_registry_resume(const char* name);
