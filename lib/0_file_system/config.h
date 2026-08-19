#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace config
{
    extern String file_path;
    extern bool is_loaded;
    extern JsonDocument config;

    void init();

} // namespace config
