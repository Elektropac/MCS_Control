#include "memreport.h"
#include "task_registry.h"
#include "logging.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Diagnostic only: prints the heap picture + each registered task's unused
// stack, so we can see exactly where RAM goes and trim precisely. Remove once
// the memory budget is sorted.
static void memreport_task(void*) {
    // Let everything start + settle first.
    vTaskDelay(pdMS_TO_TICKS(8000));
    for (;;) {
        size_t freeHeap    = esp_get_free_heap_size();
        size_t minFree     = esp_get_minimum_free_heap_size();   // lowest ever
        size_t largest     = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        size_t internalTot = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

        log_info("[mem] free=%u  min_ever=%u  largest_block=%u  internal_total=%u",
                 (unsigned)freeHeap, (unsigned)minFree,
                 (unsigned)largest, (unsigned)internalTot);

        // Per-task unused stack (high-water mark = fewest free words ever).
        // Reported in BYTES of headroom still free at the worst point.
        int n = task_registry_count();
        for (int i = 0; i < n; i++) {
            const RegisteredTask* t = task_registry_get(i);
            if (!t || !t->handle) continue;
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(t->handle); // words
            log_info("[mem]   task %-12s alloc=%5u  free_min=%5u bytes",
                     t->name, (unsigned)t->stack_size,
                     (unsigned)(hwm * sizeof(StackType_t)));
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void memreport_start() {
    xTaskCreate(memreport_task, "memreport", 3072, nullptr, 1, nullptr);
}

void memreport_mark(const char* where) {
    size_t freeHeap = esp_get_free_heap_size();
    size_t largest  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    log_info("[mem] @%-18s free=%6u  largest_block=%6u",
             where, (unsigned)freeHeap, (unsigned)largest);
}
