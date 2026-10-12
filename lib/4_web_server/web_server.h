#pragma once

#include <Ethernet.h>
#include <WiFiServer.h>

#include "web_helpers.h"

class CompatEthernetServer : public EthernetServer
{
public:
    using EthernetServer::EthernetServer;
    void begin(uint16_t port = 0);
};

namespace web_server
{
    extern CompatEthernetServer eth_server;
    extern WiFiServer wifi_server;
    void init();
    // Start listening on the WiFi interface (e.g. after a SoftAP is brought
    // up by service_ap, so joined phones reach the existing pages).
    void wifi_begin();
    void handle_client(Client &client, Request &req);
    void poll();
}
