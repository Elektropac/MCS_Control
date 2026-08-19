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
        auto is_wifi_config = config::config["connection"]["settings"]["wifi"].isNull();
        if (is_wifi_config)
        {
            return;
        }

        WiFi.disconnect(true, true);
        log_info("[wifi] Initializing WiFi...");
        make_mac();

        auto wifi_config = config::config["connection"]["settings"]["wifi"].as<JsonObject>();
        auto is_ssid = wifi_config["ssid"].isNull();
        auto is_password = wifi_config["password"].isNull();
        if (is_ssid || is_password)
        {
            log_error("[wifi] WiFi SSID or password not found in config. WiFi will not be initialized.");
            return;
        }

        auto ssid = wifi_config["ssid"].as<String>();
        auto password = wifi_config["password"].as<String>();
        log_info("[wifi] Connecting to WiFi SSID: %s", ssid);

        WiFi.setHostname(("ESP32-Niklas-" + mac).c_str());
        WiFi.begin(ssid.c_str(), password.c_str());

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
}
