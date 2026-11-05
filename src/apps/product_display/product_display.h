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
// Display power
//
// The TopBoard (5 buttons + display) has been removed, so there is no button
// to wake a sleeping panel. The OLED is therefore a FIXED display and stays
// powered on at all times. These helpers remain for the alarm overlay, which
// may need to assert the panel is on, but there is no inactivity timeout.
// -----------------------------------------------------------------

// True if the OLED panel is currently powered on (always true now).
bool product_display_panel_on();

// Ensure the panel is powered on (used by the alarm overlay). The panel is
// normally always on; this is a safe no-op in that case.
void product_display_wake();
