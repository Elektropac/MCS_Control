#pragma once
#include <Arduino.h>
#include "menu.h"

// Build tank submenu dynamically from config (call after config::init)
void tank_submenu_build();

// Get built submenu items
const MenuItem* tank_submenu_get_items();
uint8_t tank_submenu_get_count();

// Render a tank status screen
void render_tank_view(const char* name, int liters, int percent);
