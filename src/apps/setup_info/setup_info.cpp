#include "setup_info.h"
#include <ArduinoJson.h>
#include <string.h>

// config document + on-demand readers (config.h, header-only).
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
    String config_read_string(const char* section, const char* key,
                              const char* fallback);
    bool config_read_groups(JsonDocument& out);
}

String setup_info_get() {
    JsonDocument out;

    // --- guide (which setup guide built this config) ---
    out["guide"] = config::config_read_string("config", "guide", "custom");

    // --- channel supply voltages (device level) ---
    int va = 0, vb = 0;
    if (config::is_loaded) {
        JsonVariant ch = config::config["config"]["channels"];
        if (ch.isNull()) ch = config::config["channels"];
        va = ch["A"]["voltage"] | 0;
        vb = ch["B"]["voltage"] | 0;
    }
    JsonObject channels = out["channels"].to<JsonObject>();
    channels["A"] = va;
    channels["B"] = vb;

    // --- groups: count + list probes/pumps (on-demand, no rules[]) ---
    JsonArray probes = out["probes"].to<JsonArray>();
    JsonArray pumps  = out["pumps"].to<JsonArray>();
    uint8_t probe_count = 0, pump_count = 0;

    JsonDocument groupsDoc;
    if (config::config_read_groups(groupsDoc) && !groupsDoc["groups"].isNull()) {
        for (JsonObject g : groupsDoc["groups"].as<JsonArray>()) {
            const char* t = g["type"] | "";
            if (strcmp(t, "probe") == 0) {
                probe_count++;
                JsonObject p = probes.add<JsonObject>();
                p["id"]         = g["id"] | "";
                p["name"]       = g["probe"]["name"] | "";
                p["input"]      = g["probe"]["input"] | (g["channel"] | "");
                p["probe_type"] = g["probe"]["probe_type"] | "";
            } else if (strcmp(t, "pump") == 0) {
                pump_count++;
                JsonObject p = pumps.add<JsonObject>();
                p["id"]    = g["id"] | "";
                p["name"]  = g["pump"]["name"] | "";
                p["input"] = g["pump"]["input"] | (g["channel"] | "");
            }
        }
    }

    out["probe_count"] = probe_count;
    out["pump_count"]  = pump_count;

    String result;
    serializeJson(out, result);
    return result;
}
