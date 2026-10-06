#pragma once
// =======================================================
// OLED menu — carousel navigation
// =======================================================
// Horizontal (LEFT/RIGHT) switches section:
//     HOME (product image) <-> TANKS <-> PUMPS
// Vertical (UP/DOWN) scrolls items within a section:
//     one tank/pump shown at a time, UP/DOWN cycles through them.
//
// Content is dynamic: the number of tank/pump "pages" comes from the live
// probe/pump data. Empty sections show a "none connected" message.
//
// The menu owns what's on the OLED after boot. Button presses drive it;
// a background task redraws periodically so live values stay current.
// =======================================================
#include <Arduino.h>
#include "buttons.h"

// Start the menu render task. Call once at boot (after the splash).
void menu_start();

// Feed a button event into the menu (called from the buttons task).
void menu_handle_button(Button btn);

// Reset the menu to the HOME screen (e.g. on wake from sleep).
void menu_go_home();
