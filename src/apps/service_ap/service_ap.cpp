#include "service_ap.h"
#include <WiFi.h>
#include <DNSServer.h>
#include "logging.h"

// Read-only view of Niklas' WiFi station flag (same pattern as menu/OTA: we
// only READ it). We flip it true while the AP is up so web_server::poll()
// also services the WiFi interface (it guards on wifi::connected).
namespace wifi { extern bool connected; }

// web_server lives in a header-only lib; begin the WiFi listener when the AP
// comes up so the existing pages are served to joined phones. Declared here to
// avoid pulling the whole Ethernet/WiFi web header into this translation unit.
namespace web_server { void wifi_begin(); }

namespace service_ap
{
    // ---- State -------------------------------------------------------------
    static bool   s_active = false;
    static String s_ssid;
    static String s_password;
    static String s_ip;

    // Captive-portal DNS: answers every lookup with the AP IP so the phone's
    // connectivity probe hits our web server, which 302-redirects to the UI.
    static DNSServer s_dns;
    static const uint8_t DNS_PORT = 53;

    // ---- Helpers -----------------------------------------------------------

    // Unique SSID from the last 3 MAC bytes: "MCS-Control-7A3F1C".
    static String make_ssid()
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char suffix[7];
        snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
        return String("MCS-Control-") + suffix;
    }

    // 8-char password from an unambiguous set (no 0/O/1/I/l). esp_random() is
    // hardware-backed on the ESP32-S3.
    static String make_password()
    {
        static const char charset[] =
            "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";
        const size_t n = sizeof(charset) - 1;
        String out;
        out.reserve(8);
        for (int i = 0; i < 8; i++) out += charset[esp_random() % n];
        return out;
    }

    // Escape the WIFI: URI special chars (\ ; , : ").
    static String qr_escape(const String& in)
    {
        String out;
        out.reserve(in.length() + 4);
        for (size_t i = 0; i < in.length(); i++) {
            char c = in[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
            out += c;
        }
        return out;
    }

    // ---- Public API --------------------------------------------------------

    bool start()
    {
        if (s_active) return true;

        s_ssid     = make_ssid();
        s_password = make_password();

        // AP_STA: run the service AP ALONGSIDE whatever station-mode WiFi is
        // configured, so we never tear down normal networking. Ethernet is a
        // separate interface and is unaffected either way.
        WiFi.mode(WIFI_AP_STA);
        bool ok = WiFi.softAP(s_ssid.c_str(), s_password.c_str());
        if (!ok) {
            log_error("[service_ap] softAP() failed");
            return false;
        }

        s_ip     = WiFi.softAPIP().toString();   // default 192.168.4.1
        s_active = true;

        // Captive-portal DNS: resolve everything to the AP IP so the phone's
        // connectivity check reaches our web server and gets redirected.
        s_dns.setErrorReplyCode(DNSReplyCode::NoError);
        s_dns.start(DNS_PORT, "*", WiFi.softAPIP());

        // Make web_server::poll() start servicing the WiFi listener so joined
        // phones get the existing pages. (It guards on wifi::connected.)
        wifi::connected = true;
        web_server::wifi_begin();

        log_info("[service_ap] AP up: ssid=%s ip=%s", s_ssid.c_str(), s_ip.c_str());
        return true;
    }

    void stop()
    {
        if (!s_active) return;
        s_dns.stop();
        WiFi.softAPdisconnect(true);
        // Drop back to station-only; leaves any real STA connection logic to
        // the normal wifi module. We just remove the AP.
        WiFi.mode(WIFI_STA);
        s_active   = false;
        s_ssid     = "";
        s_password = "";
        s_ip       = "";
        log_info("[service_ap] AP stopped");
    }

    bool toggle() { return s_active ? (stop(), false) : start(); }

    void poll()
    {
        if (s_active) s_dns.processNextRequest();
    }

    bool   is_active() { return s_active; }
    String ssid()      { return s_ssid; }
    String password()  { return s_password; }
    String ip()        { return s_ip; }

    String qr_payload()
    {
        if (!s_active) return String();
        return String("WIFI:T:WPA;S:") + qr_escape(s_ssid) +
               ";P:" + qr_escape(s_password) + ";;";
    }

} // namespace service_ap
