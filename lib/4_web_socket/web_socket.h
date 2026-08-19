#pragma once

#include <Arduino.h>

namespace web_socket
{
    extern bool is_connected;
    extern bool is_secure;
    bool run(String host, int port, String path);
    void init();
    void sendMessage(const String &message);
    void poll();
} // namespace web_socket
