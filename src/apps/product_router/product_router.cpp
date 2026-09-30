#include "product_router.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/flow_guard/flow_guard.h"
#include "apps/poseidon/poseidon.h"
#include "logging.h"
#include <ArduinoJson.h>

// config is defined in config.h (header-only). We only read it here.
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
}

void start_product_apps() {
    String product = "cloudgauge2";  // default
    if (config::is_loaded && !config::config["product"].isNull()) {
        product = config::config["product"].as<String>();
    }
    log_info("[product_router] Product: %s", product.c_str());

    if (product == "poseidon") {
        poseidon_init();
        poseidon_start_task();
        return;
    }

    // cloudgauge2 (and sapphire_lite_mini for now) run the probe reader.
    if (product != "cloudgauge2" && product != "sapphire_lite_mini") {
        log_error("[product_router] Unknown product '%s', defaulting to cloudgauge2", product.c_str());
    }
    cloudgauge_init();
    flow_guard_start_task();
    cloudgauge_start_task();
}
