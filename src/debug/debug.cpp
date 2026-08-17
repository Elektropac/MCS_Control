#include "debug.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

void debug_task_list() {
    Serial.println("=== TASK LIST ===");
    Serial.printf("Total FreeRTOS tasks: %u\n", uxTaskGetNumberOfTasks());
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
