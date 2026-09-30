#pragma once

#include <ArduinoJson.h>
#include "file_system.h"
#include "LittleFS.h"

#include "logging.h"

namespace config
{
    String file_path = "/config.json";
    bool is_loaded = false;
    JsonDocument config;

    void init()
    {
        if (file_system::is_mounted == false)
        {
            // Serial.println("[config] File system not mounted. Cannot load configuration.");
            log_info("[config] File system not mounted. Cannot load configuration.");
            return;
        }

        File configFile = LittleFS.open(file_path, "r");
        if (!configFile)
        {
            // Serial.println("[config] Failed to open config file. Using default configuration.");
            log_info("[config] Failed to open config file. Using default configuration.");
            return;
        }

        DeserializationError error = deserializeJson(config, configFile);
        if (error)
        {
            // Serial.println("[config] Failed to parse config file. Using default configuration.");
            log_info("[config] Failed to parse config file. Using default configuration.");
            config.clear(); // Clear the document to ensure it's empty
            is_loaded = false;
        }
        else
        {
            // Serial.println("[config] Configuration loaded successfully.");
            log_info("[config] Configuration loaded successfully.");
            is_loaded = true;
        }
        
        configFile.close();
    }

    // -----------------------------------------------------------------
    // On-demand read of ONE string field from /config.json.
    //
    // Uses an ArduinoJson filter so ONLY the requested key is deserialised —
    // the rest of the file is skipped while streaming. RAM use stays tiny and
    // constant regardless of how big the config grows (probes, pumps, rules).
    // This is the config-arkitektur.md rule: never load the whole file to RAM.
    //
    //   config_read_string("config", "guide")  -> reads config.config.guide
    //
    // Returns `fallback` if the file/section/key is missing or on any error.
    // -----------------------------------------------------------------
    String config_read_string(const char* section, const char* key,
                              const char* fallback = "")
    {
        if (file_system::is_mounted == false) return String(fallback);

        File f = LittleFS.open(file_path, "r");
        if (!f) return String(fallback);

        // Filter: keep only config[section][key], skip everything else.
        JsonDocument filter;
        filter[section][key] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(
            doc, f, DeserializationOption::Filter(filter));
        f.close();

        if (err) {
            log_info("[config] read_string parse error: %s", err.c_str());
            return String(fallback);
        }

        JsonVariant v = doc[section][key];
        if (v.isNull() || !v.is<const char*>()) return String(fallback);
        return String(v.as<const char*>());
    }

} // namespace config
