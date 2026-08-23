#include "function_silo.h"
#include "rgb.h"

namespace function_silo
{
    // External handler callback (set from src/ code)
    static String (*s_ext_handler)(const String&, JsonDocument&) = nullptr;

    void register_external_handler(String (*handler)(const String&, JsonDocument&)) {
        s_ext_handler = handler;
    }

    String run_function_silo(JsonDocument &json_packet)
    {
        bool is_subject = json_packet["subject"].isNull();

        if (is_subject)
        {
            Serial.println("[function_silo] Received message does not contain 'subject' field.");
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

        // -- external handlers (test api etc.) --
        if (s_ext_handler) {
            String ext_result = s_ext_handler(function_name, json_packet);
            if (ext_result.length() > 0) return ext_result;
        }

        return "";
    }
}
