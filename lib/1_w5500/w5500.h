#pragma once

#include <Arduino.h>
#include <Ethernet.h>

// Define W5500 pin assignments
#define W5500_CS 14  // Chip Select pin
#define W5500_RST 9  // Reset pin
#define W5500_INT 10 // Interrupt pin

#define W5500_SCK 13  // Clock pin
#define W5500_MISO 12 // MISO pin
#define W5500_MOSI 11 // MOSI pin

namespace w5500
{

    extern bool connected;
    extern String mode;
    extern byte mac_byte[6];
    extern String mac;
    extern IPAddress local_ip;
    extern IPAddress gateway_ip;
    extern IPAddress subnet_mask;

    String make_mac();
    String find_mode();
    int run_dhcp();
    int run_link_local();
    int run_static();
    void init();

}