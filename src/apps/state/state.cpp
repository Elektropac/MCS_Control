#include "apps/state/state.h"

#include <ArduinoJson.h>

#include "apps/pump/pump.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/poseidon/poseidon.h"
#include "apps/rule_engine/rule_engine.h"

// Parse a sub-getter's JSON string into `dst[key]`. If the string is empty
// or not valid JSON, the key is left absent — the aggregator never fails
// just because one app is inactive or returned nothing.
static void nest_json(JsonDocument &dst, const char *key, const String &src) {
    if (src.length() == 0) {
        return;
    }
    JsonDocument sub;
    DeserializationError err = deserializeJson(sub, src);
    if (err) {
        // Keep it robust: on parse error just skip this section rather than
        // emitting broken JSON. (Shouldn't happen — the getters build valid
        // JSON — but this makes the aggregate impossible to corrupt.)
        return;
    }
    dst[key] = sub;
}

String state_get_all() {
    JsonDocument doc;

    // Activity flags — the actual "what is the device doing" answer.
    // Both are read-only runtime checks (nozzle lifted / relay on).
    doc["fueling_active"] = rule_engine_fueling_active();
    doc["pump_active"]    = pump_any_active();

    // Nest the existing per-domain getters. Each is config-dynamic, so the
    // aggregate reflects exactly what this unit is set up with.
    nest_json(doc, "pumps",  pump_get_all());
    nest_json(doc, "probes", cloudgauge_get_all());
    nest_json(doc, "io",     poseidon_io_get_all());

    String result;
    serializeJson(doc, result);
    return result;
}
