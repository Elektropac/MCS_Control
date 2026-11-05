#include "capability_router.h"
#include "apps/probes/probes.h"
#include "apps/pumps/pumps.h"
#include "apps/flow_guard/flow_guard.h"
#include "apps/rule_engine/rule_engine.h"
#include "logging.h"
#include <ArduinoJson.h>
#include <string.h>

// On-demand groups reader (config.h).
namespace config {
    bool config_read_groups(JsonDocument& out);
}

// Capability detection: look at what the config actually CONTAINS (probe
// groups, pump groups) and start the matching apps. No "product" concept —
// the config describes capabilities, the firmware reacts to them.
void start_apps() {
    JsonDocument groupsDoc;
    uint8_t probe_groups = 0;
    uint8_t pump_groups  = 0;

    if (config::config_read_groups(groupsDoc) && !groupsDoc["groups"].isNull()) {
        for (JsonObject g : groupsDoc["groups"].as<JsonArray>()) {
            const char* t = g["type"] | "";
            if (strcmp(t, "probe") == 0) probe_groups++;
            else if (strcmp(t, "pump") == 0) pump_groups++;
        }
    }
    log_info("[capabilities] probe groups: %d, pump groups: %d", probe_groups, pump_groups);

    // Probes → start the cloudgauge measurement task.
    if (probe_groups > 0) {
        probes_init();
        flow_guard_start_task();
        probes_start_task();
        log_info("[capabilities] started probe measurement");
    }

    // Pumps → start the pump/fueling controller.
    if (pump_groups > 0) {
        pumps_init();
        pumps_start_task();
        log_info("[capabilities] started pump controller");
    }

    // Shared rule engine → executes rules[] for BOTH probe and pump groups.
    // One config-driven engine (rule-engine-reference.md). Measurement stays
    // in cloudgauge; the engine only reads probe state + drives relays/state.
    if (probe_groups > 0 || pump_groups > 0) {
        rule_engine_init();
        rule_engine_start_task();
        log_info("[capabilities] started shared rule engine");
    }

    if (probe_groups == 0 && pump_groups == 0) {
        log_error("[capabilities] no probe or pump groups in config — nothing to start");
    }
}
