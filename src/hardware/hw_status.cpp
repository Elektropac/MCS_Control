#include "hw_status.h"

static bool s_available[HW_COUNT] = { false };

void hw_set_available(HwDevice dev, bool available) {
    if (dev < HW_COUNT) s_available[dev] = available;
}

bool hw_available(HwDevice dev) {
    if (dev < HW_COUNT) return s_available[dev];
    return false;
}
