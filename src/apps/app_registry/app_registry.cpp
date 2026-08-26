#include "app_registry.h"
#include "../demo_sensor/demo_sensor.h"

namespace app_registry {

    // ─── Register your apps here ───────────────────────────────
    static const AppEntry s_apps[] = {
        //  type              init                task                stack   prio
        { "demo_sensor",    demo_sensor_init,   demo_sensor_task,   3072,   2 },
        // { "pump_controller", pump_ctrl_init,  pump_ctrl_task,     4096,   3 },
        // { "tank_gauge",      tank_gauge_init, tank_gauge_task,    3072,   2 },
    };
    // ───────────────────────────────────────────────────────────

    static const int s_app_count = sizeof(s_apps) / sizeof(s_apps[0]);

    const AppEntry* get_entries(int& count) {
        count = s_app_count;
        return s_apps;
    }

    void start_apps(const JsonArray& functions) {
        for (JsonObject func : functions) {
            const char* type = func["type"];
            if (!type) continue;

            // Find matching app entry
            for (int i = 0; i < s_app_count; i++) {
                if (strcmp(s_apps[i].type, type) == 0) {
                    // Call init with config section
                    s_apps[i].init(func);

                    // Create FreeRTOS task
                    const char* id = func["id"] | type;
                    xTaskCreate(
                        s_apps[i].task,
                        id,                     // task name (shows in debug)
                        s_apps[i].stack_size,
                        nullptr,
                        s_apps[i].priority,
                        nullptr
                    );

                    Serial.printf("[apps] Started '%s' (type=%s, stack=%d, prio=%d)\n",
                                  id, type, s_apps[i].stack_size, s_apps[i].priority);
                    break;
                }
            }
        }
    }
}
