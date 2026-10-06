#pragma once
// =======================================================
// Product display — show the product "face" on the OLED
// =======================================================
// Reads the "guide" field from /config.json (on-demand, no full parse)
// and draws the matching 128x64 product bitmap on the OLED at boot.
//
//   guide "cloudgauge2"        -> CloudGauge2 bitmap
//   guide "sapphire_lite_mini" -> Sapphire Lite Mini bitmap
//   guide "poseidon"           -> Poseidon bitmap
//   guide "bridge"             -> Bridge bitmap
//   anything else / "custom" / missing -> no image (nothing shown)
// =======================================================

// Show the MCS brand logo at boot, held briefly, before the product splash.
void mcs_logo_show_splash();

// Show the product splash for the configured guide, then hold it briefly so
// it's visible at boot. No-op (returns immediately) if the guide is unknown.
void product_display_show_splash();

// -----------------------------------------------------------------
// Display power management
//
// The OLED turns itself OFF after a period of inactivity (default 10 min)
// to protect the panel and save power. Any button press wakes it again.
// -----------------------------------------------------------------

// Start the background task that powers the OLED off after the inactivity
// timeout. Call once at boot, after the splash.
void product_display_start_power_manager();

// Register user activity (e.g. a button press): resets the inactivity timer.
// If the panel was off, wakes it (to the HOME screen) and returns true so the
// caller treats this press as "wake only". Returns false if already awake.
bool product_display_mark_activity();

// True if the OLED panel is currently powered on.
bool product_display_panel_on();

// Wake the panel (power on) without changing content or jumping to HOME, and
// reset the inactivity clock. Used by the alarm overlay so an alarm lights up
// a sleeping screen and keeps it on while active.
void product_display_wake();
