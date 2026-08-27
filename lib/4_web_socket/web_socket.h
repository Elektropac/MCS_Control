#pragma once

#include <Arduino.h>

namespace web_socket
{
    extern bool is_connected;
    extern bool is_secure;

    extern String local_host;
    extern int local_port;

    extern String global_host;
    extern int global_port;

    extern bool try_local;

    bool run(String host, int port);
    void load_config();
    void init();
    void sendMessage(const String &message);
    void poll();
} // namespace web_socket
