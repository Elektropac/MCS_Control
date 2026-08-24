#include "web_server.h"

#include "logging.h"
#include "my_wifi.h"

void CompatEthernetServer::begin(uint16_t port)
{
    (void)port;
    EthernetServer::begin();
}

namespace web_server
{
    CompatEthernetServer eth_server(80);
    WiFiServer wifi_server(80);

    void init()
    {
        log_info("[web_server] Initializing web server");
        eth_server.begin();
        if (wifi::connected) wifi_server.begin();
    }

    void handle_client(Client &client, Request &req)
    {
        if (req.path == "" || req.method == "") return;
        if (!req.path.startsWith("/")) return;

        log_info("[web_server] Request: %s %s", req.method, req.path);
        
        if (req.path.startsWith("/api/") || req.path == "/api")
        {
            handle_api_request(client, req);
            return;
        }
        
        handle_page_request(client, req);
    }

    void poll()
    {
        EthernetClient eth_client = eth_server.available();
        if (eth_client)
        {
            Request req(eth_client);
            handle_client(eth_client, req);
            eth_client.stop();
        }

        if (wifi::connected)
        {
            WiFiClient wifi_client = wifi_server.available();
            if (wifi_client)
            {
                Request req(wifi_client);
                handle_client(wifi_client, req);
                wifi_client.stop();
            }
        }
    }
}
