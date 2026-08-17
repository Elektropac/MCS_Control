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

} // namespace config
