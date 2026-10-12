#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// the common incomeing data structure is
// {
//     "subject": "****", -> function_name
//     "data"?: {...} | [{...}] -> json_object
// }

namespace function_silo
{
    String run_function_silo(JsonDocument &json_packet);
} // namespace function_silo
