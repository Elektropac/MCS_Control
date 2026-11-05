#pragma once
// =======================================================
// OLED display — auto-scrolling carousel (no buttons)
// =======================================================
// The TopBoard (5 buttons + display) has been removed. The OLED is now a
// FIXED display with no interactive navigation. A background task owns the
// panel and cycles through a set of screens on its own:
//
//     HOME (guide name) -> each TANK -> each PUMP -> INFO -> (repeat)
//
// Content is dynamic: the tank/pump screens come from live probe/pump data.
// An alarm (low / overfill) still takes over the whole screen regardless of
// which carousel screen is showing, and clears automatically when resolved.
//
// A later step will make the carousel's contents + timing config-defined and
// add slide transitions. For now this is the cleaned-up, button-free base.
// =======================================================
#include <Arduino.h>

// Start the display task. Call once at boot (after the splash).
void menu_start();
