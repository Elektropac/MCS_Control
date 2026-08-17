#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"


namespace wifi
{
    bool connected = false;
    String mac;
    IPAddress local_ip;
    IPAddress gateway_ip;
    IPAddress subnet_mask;

    String make_mac() {
        mac = WiFi.macAddress();
        return mac;
    }

    void init() 
    {
        auto is_wifi_config = config::config["connection"]["settings"]["wifi"].isNull();
        
        if (is_wifi_config) {
            return;
        }
        
        WiFi.disconnect(true, true); // Disconnect from any previous WiFi connections
        
        Serial.println("[wifi] Initializing WiFi...");
        make_mac();
        
        auto wifi_confg = config::config["connection"]["settings"]["wifi"].as<JsonObject>();
        auto is_ssid = wifi_confg["ssid"].isNull();
        auto is_password = wifi_confg["password"].isNull();
        
        if (is_ssid || is_password) {
            Serial.println("[wifi] WiFi SSID or password not found in config. WiFi will not be initialized.");
            return;
        }
        
        auto ssid = wifi_confg["ssid"].as<String>();
        auto password = wifi_confg["password"].as<String>();

        Serial.printf("[wifi] Connecting to WiFi SSID: %s\n", ssid.c_str());

        WiFi.setHostname(("ESP32-Niklas-" + mac).c_str());
        WiFi.begin(ssid.c_str(), password.c_str());

        int attempts = 0;
        // waits for WiFi status to be connected or until 20 attempts
        Serial.print("[wifi] ");
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        Serial.println("[wifi] wait complete");

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            local_ip = WiFi.localIP();
            gateway_ip = WiFi.gatewayIP();
            subnet_mask = WiFi.subnetMask();
            Serial.println("[wifi] WiFi connected successfully");
            Serial.printf("[wifi] Local IP: %s\n", local_ip.toString().c_str());
            Serial.printf("[wifi] Gateway IP: %s\n", gateway_ip.toString().c_str());
            Serial.printf("[wifi] Subnet Mask: %s\n", subnet_mask.toString().c_str());
        } else {
            connected = false;
            Serial.println("[wifi] Failed to connect to WiFi.");
            WiFi.disconnect(true, true); // Disconnect from any previous WiFi connections
        }

    }

} // namespace wifi
