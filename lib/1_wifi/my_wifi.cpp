#include "my_wifi.h"

#include <Arduino.h>
#include <esp_mac.h>

#include "config.h"
#include "logging.h"

namespace wifi
{
    bool connected = false;
    String mac;
    IPAddress local_ip;
    IPAddress gateway_ip;
    IPAddress subnet_mask;

    String make_mac()
    {
        // WiFi.macAddress() returns 00:00:... before the WiFi driver is up.
        // BLE init now runs early in boot (before the network stack), so read
        // the MAC straight from eFuse, which is always available.
        uint8_t m[6] = {0};
        if (esp_read_mac(m, ESP_MAC_WIFI_STA) == ESP_OK &&
            (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) != 0) {
            char buf[18];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            mac = String(buf);
        } else {
            mac = WiFi.macAddress();   // fallback once WiFi is up
        }
        return mac;
    }

    void init()
    {
        if (config::wifi_config.use_wifi == false)
        {
            return;
        }

        WiFi.disconnect(true, true);
        make_mac();

        log_info("[wifi] Connecting to WiFi SSID: %s", config::wifi_config.ssid.c_str());

        WiFi.setHostname(("control-" + mac).c_str());
        WiFi.begin(config::wifi_config.ssid.c_str(), config::wifi_config.password.c_str());

        int attempts = 0;
        log_info("[wifi] now waiting for WiFi connection...");
        while (WiFi.status() != WL_CONNECTED && attempts < 20)
        {
            delay(500);
            attempts++;
        }

        log_info("[wifi] wait complete");
        if (WiFi.status() == WL_CONNECTED)
        {
            connected = true;
            local_ip = WiFi.localIP();
            gateway_ip = WiFi.gatewayIP();
            subnet_mask = WiFi.subnetMask();
            log_info("[wifi] Local IP: %s, Gateway: %s, Subnet Mask: %s", local_ip.toString().c_str(), gateway_ip.toString().c_str(), subnet_mask.toString().c_str());
        }
        else
        {
            connected = false;
            log_error("[wifi] Failed to connect to WiFi.");
            WiFi.disconnect(true, true);
        }
    }

    void poll()
    {
        // todo: make sure to check if WiFi is still connected and reconnect if necessary
    }
}
