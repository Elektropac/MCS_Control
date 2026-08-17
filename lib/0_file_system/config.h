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

    // Create a minimal default config if none exists
    void create_default() {
        File f = LittleFS.open(file_path, "w");
        if (!f) {
            log_error("[config] Cannot create default config file.");
            return;
        }

        f.print(R"({
  "type": "control_config",
  "version": 1,
  "connection": {
    "settings": {
      "ethernet": {
        "dhcp": true
      }
    }
  }
})");
        f.close();
        log_info("[config] Default config created.");
    }

    void init()
    {
        if (file_system::is_mounted == false)
        {
            log_info("[config] File system not mounted. Cannot load configuration.");
            return;
        }

        // Create default if missing
        if (!LittleFS.exists(file_path)) {
            create_default();
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
            config.clear();
            is_loaded = false;
        }
        else
        {
            log_info("[config] Configuration loaded successfully.");
            is_loaded = true;
        }
        
        configFile.close();
    }

} // namespace config
