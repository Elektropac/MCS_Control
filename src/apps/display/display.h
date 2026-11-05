#pragma once
// =======================================================
// display — the OLED app (boot splash + auto-scrolling carousel)
// =======================================================
// This is the single owner of the OLED. The TopBoard (5 buttons + display)
// has been removed, so there is NO interactive navigation: the OLED is a fixed
// display driven entirely by config / web / BLE.
//
// Lifecycle:
//   1. display_boot_splash()  — MCS brand logo at boot (blocking, ~2s)
//   2. display_start()        — spawns the carousel task, which from then on
//                               owns the panel and cycles through screens:
//                                   HOME -> each TANK -> each PUMP -> INFO
//
// Content is dynamic (tank/pump screens come from live probe/pump data). An
// alarm (low / overfill) takes over the whole screen regardless of which
// carousel screen is showing, and clears automatically when resolved.
//
// A later step will make the carousel's contents + timing config-defined and
// add slide transitions. For now this is the cleaned-up, button-free base.
//
// (This app merges the former `menu` + `product_display` apps into one — there
// is only one thing that owns the OLED now, so there is only one app for it.)
// =======================================================
#include <Arduino.h>

// Show the MCS brand logo at boot, held briefly. Blocking. Call once, before
// display_start(), while the rest of setup() continues afterwards.
void display_boot_splash();

// Start the carousel task. Call once at boot, after the splash.
void display_start();

// -----------------------------------------------------------------
// Panel power
//
// The OLED has no buttons to wake it, so it is a FIXED display and stays on.
// These remain for the alarm overlay (assert the panel is on); there is no
// inactivity timeout.
// -----------------------------------------------------------------

// True if the OLED panel is currently powered on (always true now).
bool display_panel_on();

// Ensure the panel is powered on (used by the alarm overlay). Safe no-op when
// already on, which is the normal case.
void display_wake();
