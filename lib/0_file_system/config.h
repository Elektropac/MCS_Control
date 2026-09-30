#pragma once

#include <ArduinoJson.h>
#include <string.h>
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
    bool config_read_groups(JsonDocument& out)
    {
        out.clear();
        if (file_system::is_mounted == false) return false;

        File f = LittleFS.open(file_path, "r");
        if (!f) return false;

        // Filter: keep group id/type/channel + the probe/pump blocks, but
        // NOT rules[] (skipped while streaming to keep RAM low).
        JsonDocument filter;
        JsonObject gf = filter["groups"].add<JsonObject>();
        gf["id"] = true;
        gf["type"] = true;
        gf["channel"] = true;
        gf["probe"] = true;
        gf["pump"] = true;

        DeserializationError err = deserializeJson(
            out, f, DeserializationOption::Filter(filter));
        f.close();

        if (err) {
            log_info("[config] read_groups parse error: %s", err.c_str());
            out.clear();
            return false;
        }
        return true;
    }

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
    bool config_read_group_rules(const char* group_id, JsonDocument& out)
    {
        out.clear();
        if (file_system::is_mounted == false) return false;

        File f = LittleFS.open(file_path, "r");
        if (!f) return false;

        // Filter: keep each group's id + rules only (probe/pump blocks skipped).
        JsonDocument filter;
        JsonObject gf = filter["groups"].add<JsonObject>();
        gf["id"] = true;
        gf["rules"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(
            doc, f, DeserializationOption::Filter(filter));
        f.close();

        if (err) {
            log_info("[config] read_group_rules parse error: %s", err.c_str());
            return false;
        }

        JsonArray outArr = out.to<JsonArray>();
        for (JsonObject g : doc["groups"].as<JsonArray>()) {
            const char* id = g["id"] | "";
            if (strcmp(id, group_id) != 0) continue;
            for (JsonObject r : g["rules"].as<JsonArray>()) {
                outArr.add(r);
            }
            return true;
        }
        return false;   // group not found
    }

} // namespace config
