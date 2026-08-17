#pragma once
#include <Arduino.h>
#include "menu.h"

// Register of all possible menu items (firmware knows about these)
// Config file selects which ones to show and in what order.

struct MenuRegistryEntry {
    const char* id;             // matches config string, e.g. "tanks"
    const char* label;          // display name
    const uint8_t** icon_frames;
    void (*action)();
    const MenuItem* submenu;
    uint8_t submenu_count;
};

// Build menu from config JSON array. Returns number of items built.
// Pass a MenuItem array (output) and max size.
uint8_t menu_build_from_config(MenuItem* out, uint8_t max_items);

// Get full registry (for fallback if no config)
const MenuRegistryEntry* menu_registry_entries();
uint8_t menu_registry_count();
