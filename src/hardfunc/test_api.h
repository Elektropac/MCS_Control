#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace test_api {
    String handle(const String& function_name, JsonDocument& json_packet);
}
