#include "w5500.h"

#include <SPI.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "logging.h"

namespace w5500
{
    bool connected = false;
    byte mac_byte[6];
    String mac;

    IPAddress local_ip;
    IPAddress gateway_ip;
    IPAddress subnet_mask;

    String make_mac()
    {
        uint64_t chip_id = ESP.getEfuseMac();
        mac_byte[0] = 0x02;
        mac_byte[1] = (chip_id >> 32) & 0xFF;
        mac_byte[2] = (chip_id >> 24) & 0xFF;
        mac_byte[3] = (chip_id >> 16) & 0xFF;
        mac_byte[4] = (chip_id >> 8) & 0xFF;
        mac_byte[5] = chip_id & 0xFF;

        mac = String(mac_byte[0], HEX) + ":" + String(mac_byte[1], HEX) + ":" +
              String(mac_byte[2], HEX) + ":" + String(mac_byte[3], HEX) + ":" +
              String(mac_byte[4], HEX) + ":" + String(mac_byte[5], HEX);
        mac.toUpperCase();
        return mac;
    }

    int run_dhcp()
    {
        log_info("[w5500] network mode: dhcp");
        return Ethernet.begin(mac_byte);
    }

    int run_link_local()
    {
        log_info("[w5500] network mode: link-local");
        local_ip.fromString("169.254.77.1");
        gateway_ip.fromString("0.0.0.0");
        subnet_mask.fromString("255.255.0.0");
        Ethernet.begin(mac_byte, local_ip, gateway_ip, gateway_ip, subnet_mask);
        return 1;
    }

    int run_static()
    {
        log_info("[w5500] network mode: static");
        local_ip.fromString(config::ethernet_config.ip);
        gateway_ip.fromString(config::ethernet_config.gateway);
        subnet_mask.fromString(config::ethernet_config.subnet);
        Ethernet.begin(mac_byte, local_ip, gateway_ip, gateway_ip, subnet_mask);
        return 1;
    }

    void init()
    {
        log_info("[w5500] Initializing W5500...");
        make_mac();

        SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);
        Ethernet.init(W5500_CS);
        
        int init_result = 0;
        if (config::ethernet_config.mode == "dhcp") init_result = run_dhcp();
        else if (config::ethernet_config.mode == "static") init_result = run_static();
        else if (config::ethernet_config.mode == "link-local") init_result = run_link_local();
    }

    void poll()
    {
        // todo: implement any necessary polling for the W5500 if needed
    }
}
