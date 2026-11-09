#pragma once

#include <Arduino.h>

namespace ntp_ethernet {
    void sync(uint8_t retries = 5, bool force = false);
}

namespace ntp_wifi {
    void sync(uint8_t retries = 5, bool force = false);
}
