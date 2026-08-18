#pragma once

#include <array>
#include <SPI.h>
#include <Ethernet.h>

#include "config.h"
#include "logging.h"

// Define W5500 pin assignments
#define W5500_CS 14  // Chip Select pin
#define W5500_RST 9  // Reset pin
#define W5500_INT 10 // Interrupt pin

#define W5500_SCK 13  // Clock pin
#define W5500_MISO 12 // MISO pin
#define W5500_MOSI 11 // MOSI pin

namespace w5500
{

    bool connected = false;
    String mode;
    byte mac_byte[6];
    String mac;
    IPAddress local_ip;
    IPAddress gateway_ip;
    IPAddress subnet_mask;

    String make_mac()
    {
        uint64_t chip_id = ESP.getEfuseMac();

        mac_byte[0] = 0x02; // locally administered, unicast
        mac_byte[1] = (chip_id >> 32) & 0xFF;
        mac_byte[2] = (chip_id >> 24) & 0xFF;
        mac_byte[3] = (chip_id >> 16) & 0xFF;
        mac_byte[4] = (chip_id >> 8) & 0xFF;
        mac_byte[5] = chip_id & 0xFF;

        mac = String(mac_byte[0], HEX) + ":" +
              String(mac_byte[1], HEX) + ":" +
              String(mac_byte[2], HEX) + ":" +
              String(mac_byte[3], HEX) + ":" +
              String(mac_byte[4], HEX) + ":" +
              String(mac_byte[5], HEX);

        mac.toUpperCase();

        return mac;
    }

    // modes: dhcp, static, link-local
    String find_mode()
    {
        auto is_eth_config = config::config["connection"]["settings"]["ethernet"].isNull();

        if (is_eth_config)
        {
            return "link-local";
        }

        auto eth_config = config::config["connection"]["settings"]["ethernet"].as<JsonObject>();
        auto is_dhcp = eth_config["dhcp"].isNull();

        if (is_dhcp)
        {
            return "link-local";
        }

        auto dhcp = eth_config["dhcp"].as<bool>();

        if (dhcp == true)
        {
            return "dhcp";
        }
        else if (dhcp == false)
        {
            return "static";
        }

        return "link-local";
    }

    int run_dhcp()
    {
        // Serial.println("[w5500] network mode: dhcp");
        log_info("[w5500] network mode: dhcp");

        return Ethernet.begin(mac_byte);
    }

    int run_link_local()
    {
        // Serial.println("[w5500] network mode: link-local");
        log_info("[w5500] network mode: link-local");

        IPAddress link_local_ip(169, 254, 77, 1); // Example link-local IP address
        IPAddress link_local_gateway(0, 0, 0, 0); // No gateway for link-local
        IPAddress link_local_subnet(255, 255, 0, 0);

        Ethernet.begin(mac_byte, link_local_ip, link_local_gateway, link_local_gateway, link_local_subnet);
        return 1;
    }

    int run_static()
    {
        // Serial.println("[w5500] network mode: static");
        log_info("[w5500] network mode: static");

        auto eth_config = config::config["connection"]["settings"]["ethernet"].as<JsonObject>();

        auto is_ip = eth_config["ip"].isNull();
        auto is_gateway = eth_config["gateway"].isNull();
        auto is_subnet = eth_config["subnet"].isNull();

        if (is_ip || is_gateway || is_subnet)
        {
            return run_link_local();
        }

        auto static_local_ip = eth_config["ip"].as<String>();
        auto static_gateway_ip = eth_config["gateway"].as<String>();
        auto static_subnet_mask = eth_config["subnet"].as<String>();

        local_ip.fromString(static_local_ip);
        gateway_ip.fromString(static_gateway_ip);
        subnet_mask.fromString(static_subnet_mask);

        Ethernet.begin(mac_byte, local_ip, gateway_ip, gateway_ip, subnet_mask);
        return 1;
    }

    void init()
    {
        // Serial.println("[w5500] Initializing W5500...");
        log_info("[w5500] Initializing W5500...");
        make_mac();
        mode = find_mode();

        // Initialize Ethernet with W5500
        SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);
        Ethernet.init(W5500_CS); // Set the CS pin for W5500
        int init_result = 0;

        if (mode == "dhcp")
        {
            init_result = run_dhcp();
        }
        else if (mode == "static")
        {
            init_result = run_static();
        }
        else if (mode == "link-local")
        {
            init_result = run_link_local();
        }

        if (init_result)
        {
            local_ip = Ethernet.localIP();
            gateway_ip = Ethernet.gatewayIP();
            subnet_mask = Ethernet.subnetMask();

            log_info("[w5500] IP: %s, Gateway: %s, Subnet Mask: %s", local_ip.toString(), gateway_ip.toString(), subnet_mask.toString());
            connected = true;
        }
        else
        {
            mode = "link-local";

            run_link_local();

            local_ip = Ethernet.localIP();
            gateway_ip = Ethernet.gatewayIP();
            subnet_mask = Ethernet.subnetMask();
            connected = true;
        }
    }

}