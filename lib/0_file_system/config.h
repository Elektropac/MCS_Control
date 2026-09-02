#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct WIFI_CONFIG
{
    bool use_wifi = false;
    String ssid;
    String password;
};

struct ETHERNET_CONFIG
{
    String mode = "local-link"; // modes: "dhcp" or "static" or "local-link"

    String ip;
    String gateway;
    String subnet;
};

struct SERVER_CONFIG
{
    String global_host;
    int global_port;
    
    bool try_local = false;
    String local_host;
    int local_port;
};

namespace config
{
    extern String file_path;
    extern bool is_loaded;
    extern JsonDocument config;

    extern WIFI_CONFIG wifi_config;
    extern ETHERNET_CONFIG ethernet_config;
    extern String internet_client;
    extern SERVER_CONFIG server_config;

    void init();

} // namespace config
