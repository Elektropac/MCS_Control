#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include "file_system.h"
#include "LittleFS.h"
#include "logging.h"

struct WIFI_CONFIG
{
    bool use_wifi = false;
    String ssid;
    String password;
};

struct ETHERNET_CONFIG
{
    String mode = "local-link"; // modes: "dhcp" or "static" or "local-link"

    String ip;
    String gateway;
    String subnet;
};

struct SERVER_CONFIG
{
    String global_host;
    int global_port;

    bool try_local = false;
    String local_host;
    int local_port;
};

namespace config
{
    extern String file_path;
    extern bool is_loaded;
    extern JsonDocument config;

    extern String config_version;
    extern WIFI_CONFIG wifi_config;
    extern ETHERNET_CONFIG ethernet_config;
    extern String internet_client;
    extern SERVER_CONFIG server_config;

    void init();

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
                              const char* fallback = "");

    // -----------------------------------------------------------------
    // On-demand read of the groups[] array WITHOUT the heavy rules[].
    //
    // Loads only each group's metadata + its probe/pump block into `out`,
    // filtering OUT the (potentially large) rules[] arrays. Peak RAM scales
    // with the number of groups + their blocks, not the whole file.
    //
    //   JsonDocument groups;
    //   config_read_groups(groups);
    //   for (JsonObject g : groups["groups"].as<JsonArray>()) { ... }
    //
    // Returns true on success (out["groups"] is an array, possibly empty).
    // -----------------------------------------------------------------
    bool config_read_groups(JsonDocument& out);

    // -----------------------------------------------------------------
    // On-demand read of ONE group's rules[] by group id.
    //
    // Streams the whole groups[] but keeps only id + rules for each group,
    // then copies out just the matching group's rules into `out` as an
    // array. Per config-arkitektur.md we handle one group's rules at a time
    // so peak RAM stays bounded by a single group, not the whole file.
    //
    //   JsonDocument rules;
    //   config_read_group_rules("probe_1", rules);
    //   for (JsonObject r : rules.as<JsonArray>()) { ... }
    //
    // Returns true if the group was found (out is its rules array; may be
    // empty). Returns false on error or unknown group.
    // -----------------------------------------------------------------
    bool config_read_group_rules(const char* group_id, JsonDocument& out);

} // namespace config
