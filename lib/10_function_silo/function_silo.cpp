#include "function_silo.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "logging.h"
#include "config.h"

#include "apps/probes/probes.h"
#include "apps/io/io.h"
#include "apps/setup_info/setup_info.h"
#include "apps/pumps/pumps.h"
#include "apps/rule_engine/rule_engine.h"
#include "apps/state/state.h"
#include "apps/fueling_auth/fueling_auth.h"
#include "apps/test_api/test_api.h"
#include "apps/ota/ota.h"

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

        // -- config --

        if (function_name == "config_get")
        {
            String result;
            serializeJson(config::config, result);
            return result;
        }
        else if (function_name == "setup_get")
        {
            return setup_info_get();
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
            return probes_get_all();
        }
        else if (function_name == "cloudgauge_get")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* input = json_object["input"];
            return probes_get(input ? input : "");
        }

        // -- pump / fueling --

        else if (function_name == "pump_get_all")
        {
            return pumps_get_all();
        }
        else if (function_name == "pump_quality_reset")
        {
            pumps_quality_reset();
            return String("{\"result\":\"quality reset\"}");
        }
        else if (function_name == "rule_debug")
        {
            return rule_engine_debug();
        }
        else if (function_name == "pump_get")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* id = json_object["id"];
            return pumps_get(id ? id : "");
        }

        // -- ota (firmware update, web/ota.html) --

        else if (function_name == "ota_status")
        {
            return otaStatusJson();
        }
        else if (function_name == "ota_check")
        {
            bool ok = otaRequestCheck();
            return String("{\"result\":\"") + (ok ? "check queued" : "busy") + "\"}";
        }
        else if (function_name == "ota_update")
        {
            bool ok = otaRequestUpdate();
            return String("{\"result\":\"") + (ok ? "update queued" : "busy") + "\"}";
        }
        else if (function_name == "ota_select")
        {
            JsonObject data = json_packet["data"].as<JsonObject>();
            String version = data.isNull() ? String("") : (data["version"] | String(""));
            otaSelectVersion(version);
            // Return current status so the UI updates immediately.
            return otaStatusJson();
        }

        // -- system --

        else if (function_name == "reboot")
        {
            log_info("Rebooting system...");
            ESP.restart();
            return "{\"result\": \"Rebooting...\"}";
        }
        else if (function_name == "ping")
        {
            return "{\"result\":\"pong\"}";
        }
        else if (function_name == "log_get")
        {
            // Firmware log. data.source = "ram" (default, live ring buffer) or
            // "flash" (persistent event log: alarms/boot/errors, survives reboot).
            JsonObject data = json_packet["data"].as<JsonObject>();
            String source = data.isNull() ? String("ram") : (data["source"] | String("ram"));
            JsonDocument d;
            d["subject"] = "log";
            d["source"] = source;
            d["text"] = (source == "flash") ? event_log_read() : log_recent();
            String out;
            serializeJson(d, out);
            return out;
        }
        else if (function_name == "log_clear")
        {
            JsonObject data = json_packet["data"].as<JsonObject>();
            String source = data.isNull() ? String("ram") : (data["source"] | String("ram"));
            if (source == "flash") { event_log_clear(); }
            else { log_clear(); }
            return "{\"result\":\"log cleared\"}";
        }
        else if (function_name == "clock_set")
        {
            // A connecting client supplies the current UTC epoch (seconds) so
            // the device can stamp events with real time until reboot.
            JsonObject data = json_packet["data"].as<JsonObject>();
            uint32_t epoch = data.isNull() ? 0 : (uint32_t)(data["utc"] | 0);
            clock_set_utc(epoch);
            JsonDocument d;
            d["subject"] = "clock";
            d["has_time"] = clock_has_time();
            d["utc"] = clock_now_utc();
            String out; serializeJson(d, out);
            return out;
        }

        // -- poseidon io --

        else if (function_name == "io_get_all")
        {
            return io_get_all();
        }
        else if (function_name == "io_get")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* pin = json_object["pin"];
            return io_get(pin ? pin : "");
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
            return io_set(pin ? pin : "", state);
        }
        else if (function_name == "io_reset_pulses")
        {
            JsonObject json_object = json_packet["data"].as<JsonObject>();
            const char* pin = json_object["pin"];
            return io_reset_pulses(pin ? pin : "");
        }

        // -- global state aggregator (read-only) --
        else if (function_name == "state_get_all")
        {
            return state_get_all();
        }

        // -- fuelling authorization + transaction delivery --
        else if (function_name == "auth_challenge")
        {
            return fueling_auth_challenge();
        }
        else if (function_name == "fuel_authorize")
        {
            return fueling_auth_authorize(json_packet["data"].as<JsonObject>());
        }
        else if (function_name == "fuel_cancel")
        {
            return fueling_auth_cancel(json_packet["data"].as<JsonObject>());
        }
        else if (function_name == "txn_pending")
        {
            return fueling_auth_pending();
        }
        else if (function_name == "txn_ack")
        {
            return fueling_auth_ack(json_packet["data"].as<JsonObject>());
        }

        // -- hardware test API (web/test.html) --
        if (function_name.startsWith("test_"))
        {
            String r = test_api_run(function_name, json_packet["data"].as<JsonObject>());
            if (r.length() > 0) return r;
        }

        // No matching subject
        return "";
    }
} // namespace function_silo
