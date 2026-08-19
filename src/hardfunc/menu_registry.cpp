#include "menu_registry.h"
#include "menu_icons.h"
#include "menu_tanks.h"
#include "menu_relays.h"
#include "menu_voltage.h"
#include "ssd1306.h"
#include "config.h"
#include <ArduinoJson.h>

// --- Actions (placeholders) ---
static void action_pumps() {}
static void action_config() {}
static void action_network() {}
static void action_reboot() {
    oled_begin();
    oled_set_font_bold();
    oled_draw_text(20, 36, "Rebooting...");
    oled_end();
    ESP.restart();
}
static void action_diagnostics() {}
static void action_relays_menu();  // forward declare
static void action_voltage_menu();  // forward declare

// --- Full registry of all possible menu items ---
static MenuRegistryEntry s_registry[] = {
    { "tanks",       "Tanks",       icon_tanks_frames,   nullptr,             nullptr, 0 },
    { "pumps",       "Pumps",       icon_pumps_frames,   action_pumps,        nullptr, 0 },
    { "relays",      "Relays",      icon_config_frames,  action_relays_menu,  nullptr, 0 },
    { "voltage",     "Voltage",     icon_config_frames,  action_voltage_menu, nullptr, 0 },
    { "config",      "Config",      icon_config_frames,  action_config,       nullptr, 0 },
    { "network",     "Network",     icon_network_frames, action_network,      nullptr, 0 },
    { "reboot",      "Reboot",      icon_reboot_frames,  action_reboot,       nullptr, 0 },
    { "diagnostics", "Diagnostics", icon_diag_frames,    action_diagnostics,  nullptr, 0 },
};

static const uint8_t REGISTRY_COUNT = sizeof(s_registry) / sizeof(s_registry[0]);

const MenuRegistryEntry* menu_registry_entries() {
    return s_registry;
}

uint8_t menu_registry_count() {
    return REGISTRY_COUNT;
}

void menu_registry_init() {
    // Build dynamic submenus
    tank_submenu_build();

    // Link tanks submenu to registry
    for (uint8_t i = 0; i < REGISTRY_COUNT; i++) {
        if (strcmp(s_registry[i].id, "tanks") == 0) {
            s_registry[i].submenu = tank_submenu_get_items();
            s_registry[i].submenu_count = tank_submenu_get_count();
            break;
        }
    }
}

// --- Build menu from config ---
uint8_t menu_build_from_config(MenuItem* out, uint8_t max_items) {
    uint8_t count = 0;

    // Check if config has menu.items array
    if (!config::is_loaded || config::config["menu"]["items"].isNull()) {
        // Fallback: use full registry in default order
        for (uint8_t i = 0; i < REGISTRY_COUNT && i < max_items; i++) {
            out[i].label = s_registry[i].label;
            out[i].icon_frames = s_registry[i].icon_frames;
            out[i].action = s_registry[i].action;
            out[i].submenu = s_registry[i].submenu;
            out[i].submenu_count = s_registry[i].submenu_count;
            count++;
        }
        return count;
    }

    // Build from config array
    JsonArray items = config::config["menu"]["items"].as<JsonArray>();

    for (JsonVariant item : items) {
        if (count >= max_items) break;

        const char* id = item["id"].as<const char*>();
        if (!id) continue;

        // Find in registry
        for (uint8_t r = 0; r < REGISTRY_COUNT; r++) {
            if (strcmp(id, s_registry[r].id) == 0) {
                out[count].label = s_registry[r].label;
                out[count].icon_frames = s_registry[r].icon_frames;
                out[count].action = s_registry[r].action;
                out[count].submenu = s_registry[r].submenu;
                out[count].submenu_count = s_registry[r].submenu_count;
                count++;
                break;
            }
        }
    }

    return count;
}

// --- Relay menu action ---
static void action_relays_menu() {
    relay_submenu_build();
    const MenuItem* items = relay_submenu_get_items();
    if (items && items[0].action) {
        items[0].action();
    }
}

// --- Voltage menu action ---
static void action_voltage_menu() {
    voltage_submenu_build();
    const MenuItem* items = voltage_submenu_get_items();
    if (items && items[0].action) {
        items[0].action();
    }
}
