#include "memmark.h"
#include <esp_heap_caps.h>

struct Mark { const char* label; uint32_t ms; uint32_t free_b; uint32_t largest; };
static Mark s_marks[32];
static volatile uint8_t s_n = 0;

void memmark(const char* label) {
    if (s_n >= 32) return;
    Mark &m = s_marks[s_n];
    m.label   = label;
    m.ms      = millis();
    m.free_b  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    m.largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s_n++;
}

void memmark_print() {
    Serial.println("HEAP MARKS (ms label free largest delta)");
    uint32_t prev = 0;
    for (uint8_t i = 0; i < s_n; i++) {
        const Mark &m = s_marks[i];
        long d = i ? (long)m.free_b - (long)prev : 0;
        Serial.printf("  %6lu %-22s %7lu %7lu %+7ld\n", (unsigned long)m.ms, m.label,
                      (unsigned long)m.free_b, (unsigned long)m.largest, d);
        prev = m.free_b;
    }
    Serial.printf("  now    %-22s %7lu %7lu %+7ld\n", "(current)",
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  s_n ? (long)heap_caps_get_free_size(MALLOC_CAP_8BIT) - (long)prev : 0L);
}
