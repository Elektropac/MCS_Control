#pragma once
// =======================================================
// Channels setup — device-level supply voltage per channel
// =======================================================
// Sets channel A/B supply voltage from config. This is a DEVICE property
// (applies to every input on a channel — probes, pumps, IO), so it runs
// once at boot BEFORE any capability apps start. Apps can then assume the
// channel voltage is already correct.
// =======================================================

// Read config channels and set A/B supply voltage. Safe to call before apps.
void channels_setup();
