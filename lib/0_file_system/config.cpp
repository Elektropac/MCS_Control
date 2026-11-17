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
    int    channel_voltage_a = -1;
    int    channel_voltage_b = -1;
    String site_name;

    String config_version = "false";
    WIFI_CONFIG wifi_config;
    ETHERNET_CONFIG ethernet_config;
    String internet_client = "ethernet";
    SERVER_CONFIG server_config;

    // Parse /config.json into `doc`. Sets is_loaded.
    static void load(JsonDocument& doc)
    {
        doc.clear();
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

        DeserializationError error = deserializeJson(doc, configFile);
        if (error)
        {
            log_info("[config] Failed to parse config file. Using default configuration.");
        }
        else
        {
            log_info("[config] Configuration loaded successfully.");
            is_loaded = true;
        }

        configFile.close();
    }

    void init()
    {
        // Parse the whole file ONCE into a temporary document, copy out the
        // few fields the firmware needs all the time, then let it go out of
        // scope (frees ~12 KB that used to stay allocated forever).
        JsonDocument config;
        load(config);

        if (!is_loaded)
        {
            return;
        }

        // Channel supply voltages: new format nests them under "config",
        // older files have them at top level.
        JsonVariant ch = config["config"]["channels"];
        if (ch.isNull()) ch = config["channels"];
        channel_voltage_a = ch["A"]["voltage"] | -1;
        channel_voltage_b = ch["B"]["voltage"] | -1;
        site_name = config["name"] | "";

        auto is_config_version = config["config_version"].isNull();
        if (!is_config_version)
        {
            config_version = config["config_version"].as<String>();
        }

        auto is_wifi_object = config["config"]["connection"]["settings"]["wifi"].isNull();

        if (!is_wifi_object)
        {
            wifi_config.use_wifi = config["config"]["connection"]["settings"]["wifi"]["enabled"].as<bool>();
            wifi_config.ssid = config["config"]["connection"]["settings"]["wifi"]["ssid"].as<String>();
            wifi_config.password = config["config"]["connection"]["settings"]["wifi"]["password"].as<String>();
        }

        auto is_ethernet_object = config["config"]["connection"]["settings"]["ethernet"].isNull();

        if (!is_ethernet_object)
        {
            ethernet_config.mode = config["config"]["connection"]["settings"]["ethernet"]["mode"].as<String>();

            if (ethernet_config.mode == "static")
            {
                ethernet_config.ip = config["config"]["connection"]["settings"]["ethernet"]["ip"].as<String>();
                ethernet_config.gateway = config["config"]["connection"]["settings"]["ethernet"]["gateway"].as<String>();
                ethernet_config.subnet = config["config"]["connection"]["settings"]["ethernet"]["subnet"].as<String>();
            }
        }

        auto is_internet_client = config["config"]["connection"]["internet_client"].isNull();

        if (!is_internet_client)
        {
            internet_client = config["config"]["connection"]["internet_client"].as<String>();
        }

        auto is_server_object = config["config"]["connection"]["server"].isNull();

        if (!is_server_object)
        {
            server_config.global_host = config["config"]["connection"]["server"]["global"]["host"].as<String>();
            server_config.global_port = config["config"]["connection"]["server"]["global"]["port"].as<int>();

            server_config.try_local = config["config"]["connection"]["server"]["local"].isNull() == false;
            if (server_config.try_local)
            {
                server_config.local_host = config["config"]["connection"]["server"]["local"]["host"].as<String>();
                server_config.local_port = config["config"]["connection"]["server"]["local"]["port"].as<int>();
            }
        }
    
    }

    bool config_read_filtered(JsonDocument& filter, JsonDocument& out)
    {
        out.clear();
        if (file_system::is_mounted == false) return false;
        File f = LittleFS.open(file_path, "r");
        if (!f) return false;
        DeserializationError err = deserializeJson(out, f, DeserializationOption::Filter(filter));
        f.close();
        if (err) {
            log_info("[config] read_filtered parse error: %s", err.c_str());
            out.clear();
            return false;
        }
        return true;
    }

    String config_read_all_json()
    {
        JsonDocument doc;
        load(doc);              // also refreshes is_loaded
        String out;
        if (is_loaded) serializeJson(doc, out);
        else out = "{}";
        return out;
    }

    String config_read_string(const char* section, const char* key,
                              const char* fallback)
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
}