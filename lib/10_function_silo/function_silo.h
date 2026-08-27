#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "rgb.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/poseidon/poseidon.h"


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
        
        // -- config --

        else if (function_name == "config_get")
        {
            String result;
            serializeJson(config::config, result);
            return result;
        }
        else if (function_name == "config_upload")
        {
            JsonObject data = json_packet["data"].as<JsonObject>();
            if (data.isNull()) return "{\"error\":\"no data\"}";
            
            // Save to LittleFS
            File f = LittleFS.open("/config.json", "w");
            if (!f) return "{\"error\":\"failed to open file\"}";
            serializeJsonPretty(data, f);
            f.close();
            
            // Reload config
            config::config.clear();
            config::init();
            
            // Delayed reboot — give time for HTTP response to be sent
            xTaskCreate([](void*) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP.restart();
            }, "reboot", 2048, nullptr, 1, nullptr);
            
            return "{\"result\":\"config saved, rebooting\"}";
        }

        // -- cloudgauge --

        else if (function_name == "cloudgauge_get_all")
        {
            return cloudgauge_get_all();
        }
        else if (function_name == "cloudgauge_get")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* input = json_object["input"];
            return cloudgauge_get(input ? input : "");
        }

        // -- system --

        else if (function_name == "reboot")
        {
            ESP.restart();
            return "{\"result\": \"Rebooting...\"}";
        }

        // -- poseidon io --

        else if (function_name == "io_get_all")
        {
            return poseidon_io_get_all();
        }
        else if (function_name == "io_get")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* pin = json_object["pin"];
            return poseidon_io_get(pin ? pin : "");
        }
        else if (function_name == "io_set")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* pin = json_object["pin"];
            bool state = false;
            if (!json_object["state"].isNull()) {
                if (json_object["state"].is<bool>()) {
                    state = json_object["state"].as<bool>();
                } else {
                    String s = json_object["state"].as<String>();
                    state = (s == "HIGH" || s == "high" || s == "1" || s == "true");
                }
            }
            return poseidon_io_set(pin ? pin : "", state);
        }
        else if (function_name == "io_reset_pulses")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* pin = json_object["pin"];
            return poseidon_io_reset_pulses(pin ? pin : "");
        }



        
        // Return a JsonString result
        return "";
    }
} // namespace function_silo
