#pragma once
#include <Arduino.h>
#include "menu.h"

void voltage_submenu_build();
const MenuItem* voltage_submenu_get_items();
uint8_t voltage_submenu_get_count();
