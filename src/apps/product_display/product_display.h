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

// Show the product splash for the configured guide, then hold it briefly so
// it's visible at boot. No-op (returns immediately) if the guide is unknown.
void product_display_show_splash();
