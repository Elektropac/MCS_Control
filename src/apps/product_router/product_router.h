#pragma once
// =======================================================
// Product router — starts the apps for the configured product
// =======================================================
// Reads "product" from config and starts the matching apps
// (cloudgauge2 / sapphire_lite_mini / poseidon). Kept out of
// main so setup() stays clean.
// =======================================================

void start_product_apps();
