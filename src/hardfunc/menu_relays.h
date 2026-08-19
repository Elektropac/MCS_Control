#pragma once
#include <Arduino.h>
#include "menu.h"

// Build relay submenu
void relay_submenu_build();

// Get built submenu items
const MenuItem* relay_submenu_get_items();
uint8_t relay_submenu_get_count();
