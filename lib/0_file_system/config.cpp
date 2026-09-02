#include "config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "file_system.h"
#include "logging.h"

namespace config
{
    String file_path = "/config.json";
    bool is_loaded = false;
    JsonDocument config;
    
    WIFI_CONFIG wifi_config;
    ETHERNET_CONFIG ethernet_config;
    String internet_client = "ethernet";
    SERVER_CONFIG server_config;

    void load()
    {
        config.clear();
        is_loaded = false;

        if (file_system::is_mounted == false)
        {
            log_info("[config] File system not mounted. Cannot load configuration.");
            return;
        }

        File configFile = LittleFS.open(file_path, "r");
        if (!configFile)
        {
            log_info("[config] Failed to open config file. Using default configuration.");
            return;
        }

        DeserializationError error = deserializeJson(config, configFile);
        if (error)
        {
            log_info("[config] Failed to parse config file. Using default configuration.");
            is_loaded = false;
        }
        else
        {
            log_info("[config] Configuration loaded successfully.");
            is_loaded = true;
        }

        configFile.close();
    }

    void init()
    {
        load();

        if (!is_loaded)
        {
            return;
        }

        auto is_wifi_object = config["connection"]["settings"]["wifi"].isNull();

        if (!is_wifi_object)
        {
            wifi_config.use_wifi = true;
            wifi_config.ssid = config["connection"]["settings"]["wifi"]["ssid"].as<String>();
            wifi_config.password = config["connection"]["settings"]["wifi"]["password"].as<String>();
        }

        auto is_ethernet_object = config["connection"]["settings"]["ethernet"].isNull();

        if (!is_ethernet_object)
        {
            ethernet_config.mode = config["connection"]["settings"]["ethernet"]["mode"].as<String>();

            if (ethernet_config.mode == "static")
            {
                ethernet_config.ip = config["connection"]["settings"]["ethernet"]["ip"].as<String>();
                ethernet_config.gateway = config["connection"]["settings"]["ethernet"]["gateway"].as<String>();
                ethernet_config.subnet = config["connection"]["settings"]["ethernet"]["subnet"].as<String>();
            }
        }

        auto is_internet_client = config["connection"]["internet_client"].isNull();

        if (!is_internet_client)
        {
            internet_client = config["connection"]["internet_client"].as<String>();
        }

        auto is_server_object = config["connection"]["server"].isNull();

        if (!is_server_object)
        {
            server_config.global_host = config["connection"]["server"]["global"]["host"].as<String>();
            server_config.global_port = config["connection"]["server"]["global"]["port"].as<int>();

            server_config.try_local = config["connection"]["server"]["local"].isNull() == false;
            if (server_config.try_local)
            {
                server_config.local_host = config["connection"]["server"]["local"]["host"].as<String>();
                server_config.local_port = config["connection"]["server"]["local"]["port"].as<int>();
            }
        }
    
    }
}