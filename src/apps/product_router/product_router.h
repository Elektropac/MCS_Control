#pragma once
// =======================================================
// Capability router — start apps based on what the config CONTAINS
// =======================================================
// No "product" concept. Reads the config groups[] and starts:
//   • probe groups -> cloudgauge measurement task
//   • pump groups  -> pump controller (TBD)
// Kept out of main so setup() stays clean.
// =======================================================

void start_product_apps();
