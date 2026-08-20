#pragma once

#include <ArduinoJson.h>
#include "file_system.h"
#include "LittleFS.h"
#include "logging.h"

namespace config
{
    inline String file_path = "/config.json";
    inline bool is_loaded = false;
    inline JsonDocument config;

    // Embedded default config (always available, no uploadfs needed)
    inline const char* DEFAULT_CONFIG PROGMEM = R"({
  "type": "control_config",
  "version": 1,
  "MAC_ID": "custom",
  "server_connection": {
    "server_order": ["local", "global"],
    "settings_order": ["ethernet"],
    "settings": {
      "ethernet": { "dhcp": true }
    }
  },
  "channels": {
    "A": { "channel_voltage": 24, "relay_output": "AR", "inputs": {} },
    "B": { "channel_voltage": 24, "relay_output": "BR", "inputs": {} }
  },
  "outputs": {
    "AR": { "used_by": "pump_1" },
    "BR": { "used_by": "pump_2" }
  },
  "functions": [
    { "id": "pump_1", "type": "pump_controller", "pulse_input": "A0", "nozzle_input": "A1", "relay_output": "AR", "meter": { "pulses_per_liter": 100.0, "unit": "L" } },
    { "id": "pump_2", "type": "pump_controller", "pulse_input": "B0", "nozzle_input": "B1", "relay_output": "BR", "meter": { "pulses_per_liter": 100.0, "unit": "L" } },
    { "id": "probe_1", "type": "probe", "level_input": "A2", "conversion": { "type": "linear", "input_min": 4.0, "input_max": 20.0, "output_min": 0.0, "output_max": 5000.0, "unit": "L" } },
    { "id": "probe_2", "type": "probe", "level_input": "B2", "conversion": { "type": "linear", "input_min": 4.0, "input_max": 20.0, "output_min": 0.0, "output_max": 5000.0, "unit": "L" } },
    { "id": "probe_3", "type": "probe", "level_input": "B3", "conversion": { "type": "linear", "input_min": 4.0, "input_max": 20.0, "output_min": 0.0, "output_max": 5000.0, "unit": "L" } }
  ],
  "menu": {
    "items": [
      { "id": "tanks" },
      { "id": "pumps" },
      { "id": "inputs" },
      { "id": "calibrate" },
      { "id": "status_a" },
      { "id": "status_b" },
      { "id": "status_uart" },
      { "id": "relays" },
      { "id": "voltage" },
      { "id": "config" },
      { "id": "network" },
      { "id": "diagnostics" },
      { "id": "reboot" }
    ]
  }
})";

    inline void init()
    {
        // Try loading from LittleFS first (user override)
        if (file_system::is_mounted && LittleFS.exists(file_path))
        {
            File configFile = LittleFS.open(file_path, "r");
            if (configFile)
            {
                DeserializationError error = deserializeJson(config, configFile);
                configFile.close();

                if (!error) {
                    log_info("[config] Loaded from LittleFS.");
                    is_loaded = true;
                    return;
                }

                log_info("[config] LittleFS config invalid, using embedded default.");
                config.clear();
            }
        }

        // Fallback: use embedded default
        DeserializationError error = deserializeJson(config, DEFAULT_CONFIG);
        if (!error) {
            log_info("[config] Using embedded default config.");
            is_loaded = true;
        } else {
            log_error("[config] Embedded config parse failed!");
            is_loaded = false;
        }
    }

} // namespace config
