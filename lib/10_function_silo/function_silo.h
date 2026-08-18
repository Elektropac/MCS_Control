#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "logging.h"

#include "rgb.h"


// the common incomeing data structure is
// {
//     "subject": "****", -> function_name
//     "data"?: {...} | [{...}] -> json_object
// }

namespace function_silo
{
    String run_function_silo(JsonDocument &json_packet)
    {
        bool is_subject = json_packet["subject"].isNull();

        if (is_subject)
        {
            log_error("[function_silo] Received message does not contain 'subject' field.");
            return "";
        }

        String function_name = json_packet["subject"].as<String>();

        // -- led --

        if (function_name == "led_on")
        {
            rgb::set(true);
            return "{\"result\": \"LED turned on\"}";
        }
        else if (function_name == "led_off")
        {
            rgb::set(false);
            return "{\"result\": \"LED turned off\"}";
        }
        else if (function_name == "led_toggle")
        {
            rgb::toggle();
            return "{\"result\": \"LED toggled\"}";
        }
        else if (function_name == "led_set")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();

            bool state = json_object["value"];
            rgb::set(state);
            return "{\"result\": \"LED set\"}";
        }
        
        // -- system --

        else if (function_name == "reboot")
        {
            ESP.restart();
            return "{\"result\": \"Rebooting...\"}";
        }



        
        // Return a JsonString result
        return "";
    }
} // namespace function_silo
