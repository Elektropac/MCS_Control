#include "function_silo.h"

#include "rgb.h"
#include "logging.h"

namespace function_silo
{
    String run_function_silo(JsonDocument &json_packet)
    {
        if (json_packet["subject"].isNull())
        {
            log_error("[function_silo] Received message does not contain 'subject' field.");
            return "";
        }

        String function_name = json_packet["subject"].as<String>();
        if (function_name == "led_on")
        {
            rgb::set(true);
            return "{\"result\": \"LED turned on\"}";
        }
        if (function_name == "led_off")
        {
            rgb::set(false);
            return "{\"result\": \"LED turned off\"}";
        }
        if (function_name == "led_toggle")
        {
            rgb::toggle();
            return "{\"result\": \"LED toggled\"}";
        }
        if (function_name == "led_set")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            rgb::set(json_object["value"]);
            return "{\"result\": \"LED set\"}";
        }
        if (function_name == "reboot")
        {
            ESP.restart();
            return "{\"result\": \"Rebooting...\"}";
        }
        return "";
    }
}