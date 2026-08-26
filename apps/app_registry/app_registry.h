#pragma once
// =======================================================
// App Registry — maps config "type" to app init/task
// =======================================================

#include <Arduino.h>
#include <ArduinoJson.h>

struct AppEntry {
    const char* type;                           // matches "type" in config.json
    void(*init)(const JsonObject& config);      // called once at boot
    void(*task)(void* param);                   // FreeRTOS task function
    uint32_t stack_size;                        // task stack in bytes
    uint8_t priority;                           // FreeRTOS priority (1=low, 5=high)
};

namespace app_registry {
    // Start all apps referenced in config.json "functions" array.
    // Iterates the array, finds matching AppEntry by type, starts a task.
    void start_apps(const JsonArray& functions);

    // Get registered app list (for debug/info)
    const AppEntry* get_entries(int& count);
}
