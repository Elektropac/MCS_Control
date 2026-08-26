#include "debug.h"
#include "task_registry.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

static const char* task_state_str(eTaskState state) {
    switch (state) {
        case eRunning:   return "RUNNING";
        case eReady:     return "READY";
        case eBlocked:   return "BLOCKED";
        case eSuspended: return "SUSPEND";
        case eDeleted:   return "DELETED";
        default:         return "?";
    }
}

void debug_task_list() {
    Serial.println("=== TASK LIST ===");
    Serial.printf("Total FreeRTOS tasks: %u (system + ours)\n", uxTaskGetNumberOfTasks());
    Serial.printf("Registered tasks:     %d\n\n", task_registry_count());

    Serial.printf("%-14s %-8s %-5s %-8s %-8s %-6s\n",
        "Name", "State", "Prio", "Stack", "Free", "Used%");
    Serial.println("-----------------------------------------------------------");

    for (int i = 0; i < task_registry_count(); i++) {
        const RegisteredTask* t = task_registry_get(i);
        if (!t || !t->handle) continue;

        eTaskState state = eTaskGetState(t->handle);
        uint32_t free_bytes = uxTaskGetStackHighWaterMark(t->handle);
        uint32_t used_bytes = (free_bytes < t->stack_size) ? (t->stack_size - free_bytes) : 0;
        uint8_t used_pct = (t->stack_size > 0) ? (used_bytes * 100 / t->stack_size) : 0;

        Serial.printf("%-14s %-8s %-5u %-8u %-8u %-3u%%\n",
            t->name,
            task_state_str(state),
            t->priority,
            t->stack_size,
            free_bytes,
            used_pct
        );
    }
    Serial.println();
}

void debug_runtime_stats() {
    Serial.println("=== RUNTIME ===");
    Serial.printf("Uptime:      %lu ms\n", millis());
    Serial.printf("Tick count:  %lu\n", (unsigned long)xTaskGetTickCount());
    Serial.printf("Tasks total: %u\n", uxTaskGetNumberOfTasks());
    Serial.println();
}

void debug_memory() {
    Serial.println("=== MEMORY ===");

    uint32_t heap_total = ESP.getHeapSize();
    uint32_t heap_free  = ESP.getFreeHeap();
    uint32_t heap_min   = ESP.getMinFreeHeap();
    uint32_t heap_used  = heap_total - heap_free;

    Serial.printf("Heap total:    %6u bytes\n", heap_total);
    Serial.printf("Heap used:     %6u bytes (%u%%)\n", heap_used, heap_used * 100 / heap_total);
    Serial.printf("Heap free:     %6u bytes\n", heap_free);
    Serial.printf("Heap min ever: %6u bytes\n", heap_min);
    Serial.printf("Largest block: %6u bytes\n",
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (psramFound()) {
        Serial.printf("PSRAM total:   %6u bytes\n", ESP.getPsramSize());
        Serial.printf("PSRAM free:    %6u bytes\n", ESP.getFreePsram());
    }
    Serial.println();
}

void debug_all() {
    Serial.println("\n========================================");
    Serial.println("        MCS CONTROL DEBUG DUMP");
    Serial.printf( "        Uptime: %lu ms\n", millis());
    Serial.println("========================================\n");

    debug_task_list();
    debug_runtime_stats();
    debug_memory();

    Serial.println("========================================\n");
}
