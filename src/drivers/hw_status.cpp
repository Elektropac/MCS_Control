#include "hw_status.h"

static bool s_available[HW_COUNT] = { false };

void hw_set_available(HwDevice device, bool available) {
    if (device < HW_COUNT) {
        s_available[device] = available;
    }
}

bool hw_available(HwDevice device) {
    if (device < HW_COUNT) {
        return s_available[device];
    }
    return false;
}

uint8_t hw_fail_count() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < HW_COUNT; i++) {
        if (!s_available[i]) count++;
    }
    return count;
}
