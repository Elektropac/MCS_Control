#include "function_silo.h"

#include <ArduinoJson.h>

#include "logging.h"

namespace function_silo
{
    String run_function_silo(JsonDocument &json_packet)
    {
        if (json_packet["subject"].isNull())
        {
            log_error("[function_silo] Received message does not contain 'subject' field.");
            String json_string;
            serializeJsonPretty(json_packet, json_string);
            log_error("[function_silo] Received message: %s", json_string.c_str());

            return "";
        }

        String function_name = json_packet["subject"].as<String>();
        if (function_name == "reboot")
        {
            ESP.restart();
            return "{\"result\": \"Rebooting...\"}";
        }
        return "";
    }
}