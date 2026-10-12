#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace wifi
{
    extern bool connected;
    extern String mac;
    extern IPAddress local_ip;
    extern IPAddress gateway_ip;
    extern IPAddress subnet_mask;

    String make_mac();
    void init();
    void poll();

} // namespace wifi
