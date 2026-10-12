#pragma once
// =======================================================
// service_ap — field service access point (SoftAP)
// =======================================================
// Starts the device's own WiFi access point (SoftAP) with a unique SSID and a
// generated password so a technician can join with a phone and reach the
// existing web UI (setup/tanks/pumps/ota) at the AP gateway IP — no app, no
// internet, works on any phone with a browser.
//
// The AP runs in WIFI_AP_STA mode so it coexists with the normal station-mode
// WiFi and the Ethernet web server: enabling service access never disturbs
// production networking. The web_server already listens on the WiFi interface,
// so pages are served on the AP automatically once a client joins.
//
// This module does NOT touch the OLED — the menu owns the display and renders
// the join QR itself via qr_payload(). Start/stop is driven from the menu.
// =======================================================
#include <Arduino.h>

namespace service_ap
{
    // Start the SoftAP (idempotent). Returns true if the AP is up.
    bool start();

    // Stop the SoftAP and release AP resources (leaves station mode intact).
    void stop();

    // Toggle convenience for a single menu button. Returns the new state.
    bool toggle();

    // Service the captive-portal DNS. Call regularly from the network task
    // (no-op when the AP is not active).
    void poll();

    bool is_active();

    // Active AP details (empty / "0.0.0.0" when not active).
    String ssid();
    String password();
    String ip();

    // The WiFi-join QR payload for the active AP, e.g.
    //   WIFI:T:WPA;S:MCS-Control-7A3F1C;P:ab23CDef;;
    // Empty string when not active. The menu feeds this to the QR encoder.
    String qr_payload();

} // namespace service_ap
