#include "my_wifi.h"

#include <Arduino.h>

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
        mac = WiFi.macAddress();
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

        log_info("[wifi] Connecting to WiFi SSID: %s", config::wifi_config.ssid);

        WiFi.setHostname(("ESP32-Niklas-" + mac).c_str());
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
            log_info("[wifi] Local IP: %s, Gateway: %s, Subnet Mask: %s", local_ip.toString(), gateway_ip.toString(), subnet_mask.toString());
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
