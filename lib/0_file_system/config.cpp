#include "config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "file_system.h"
#include "logging.h"

namespace config
{
    String file_path = "/config.json";
    bool is_loaded = false;
    JsonDocument config;

    void init()
    {
        config.clear();
        is_loaded = false;

        if (file_system::is_mounted == false)
        {
            log_info("[config] File system not mounted. Cannot load configuration.");
            return;
        }

        File configFile = LittleFS.open(file_path, "r");
        if (!configFile)
        {
            log_info("[config] Failed to open config file. Using default configuration.");
            return;
        }

        DeserializationError error = deserializeJson(config, configFile);
        if (error)
        {
            log_info("[config] Failed to parse config file. Using default configuration.");
            is_loaded = false;
        }
        else
        {
            log_info("[config] Configuration loaded successfully.");
            is_loaded = true;
        }

        configFile.close();
    }
}