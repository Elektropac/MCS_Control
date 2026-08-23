#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// the common incoming data structure is
// {
//     "subject": "****", -> function_name
//     "data"?: {...} | [{...}] -> json_object
// }

namespace function_silo
{
    String run_function_silo(JsonDocument &json_packet);
    void register_external_handler(String (*handler)(const String&, JsonDocument&));
}
