#pragma once

#include <Ethernet.h>
#include <WiFiServer.h>

#include "my_wifi.h"

#include "web_helpes.h"

class CompatEthernetServer : public EthernetServer
{
public:
    using EthernetServer::EthernetServer;

    void begin(uint16_t port = 0)
    {
        (void)port;
        EthernetServer::begin();
    }
};

namespace web_server
{
    CompatEthernetServer eth_server(80);
    WiFiServer wifi_server(80);

    void init()
    {
        Serial.println("[web_server] Initializing web server");
        eth_server.begin();

        if (wifi::connected)
        {
            wifi_server.begin();
        }
    }

    void handle_client(Client &client, Request &req)
    {
        if (req.path == "" || req.method == "") return;
        if (!req.path.startsWith("/")) return;
        // Handle the Ethernet client request here
        // log request details for debugging
        Serial.printf("[web_server] Request: %s %s\n", req.method.c_str(), req.path.c_str());
        bool is_api_request = req.path.startsWith("/api/") || req.path == "/api";

        if (is_api_request)
        {
            handle_api_request(client, req);
            return;
        }
        
        handle_page_request(client, req);
        return;
    }

    void poll()
    {
        EthernetClient eth_client = eth_server.available();

        if (eth_client)
        {
            eth_client.setTimeout(30); // 30 seconds for long-running tests
            String request_line = eth_client.readString();

            Request req = parse_request(request_line);

            handle_client(eth_client, req);
            eth_client.stop();
        }

        if (wifi::connected)
        {
            WiFiClient wifi_client = wifi_server.available();

            if (wifi_client)
            {
                wifi_client.setTimeout(30); // 30 seconds for long-running tests
                String request_line = wifi_client.readString();
                Request req = parse_request(request_line);

                handle_client(wifi_client, req);
                wifi_client.stop();
            }
        }
    }

} // namespace web_server
