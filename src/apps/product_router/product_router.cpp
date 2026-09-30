#include "product_router.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/pump/pump.h"
#include "apps/flow_guard/flow_guard.h"
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
void start_product_apps() {
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
        cloudgauge_init();
        flow_guard_start_task();
        cloudgauge_start_task();
        log_info("[capabilities] started probe measurement");
    }

    // Pumps → start the pump/fueling controller.
    if (pump_groups > 0) {
        pump_init();
        pump_start_task();
        log_info("[capabilities] started pump controller");
    }

    if (probe_groups == 0 && pump_groups == 0) {
        log_error("[capabilities] no probe or pump groups in config — nothing to start");
    }
}
