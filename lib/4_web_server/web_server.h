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

    // Start (or restart) the WiFi listener on demand. Used when the service AP
    // is enabled at runtime so joined phones get the existing pages without a
    // reboot. Safe to call more than once.
    void wifi_begin()
    {
        wifi_server.begin();
    }

    void handle_client(Client &client, Request &req)
    {
        if (req.path == "" || req.method == "") return;
        if (!req.path.startsWith("/")) return;
        // Handle the Ethernet client request here
        // log request details for debugging
        Serial.printf("[web_server] Request: %s %s\r\n", req.method.c_str(), req.path.c_str());
        bool is_api_request = req.path.startsWith("/api/") || req.path == "/api";

        if (is_api_request)
        {
            handle_api_request(client, req);
            return;
        }
        
        handle_page_request(client, req);
        return;
    }

    // Read a full HTTP request without blocking for the stream's ~1s timeout.
    // readString() always stalls until the inter-byte timeout elapses (~1000ms
    // on every request). Instead we read bytes as they arrive and return as
    // soon as the request is complete: headers terminator (\r\n\r\n) seen AND
    // the body (per Content-Length) fully received. A short wall-clock cap
    // guards against a stalled/partial client.
    static String read_full_request(Client &client)
    {
        String raw;
        raw.reserve(512);
        const uint32_t start = millis();
        const uint32_t MAX_MS = 250;          // hard cap per request
        int header_end = -1;
        int content_length = -1;

        while (millis() - start < MAX_MS)
        {
            while (client.available())
            {
                raw += (char)client.read();

                // Detect end of headers once.
                if (header_end < 0)
                {
                    header_end = raw.indexOf("\r\n\r\n");
                    if (header_end >= 0)
                    {
                        // Parse Content-Length (case-insensitive).
                        String h = raw.substring(0, header_end);
                        h.toLowerCase();
                        int ci = h.indexOf("content-length:");
                        if (ci >= 0)
                        {
                            int ve = h.indexOf("\r\n", ci);
                            if (ve < 0) ve = h.length();
                            content_length =
                                h.substring(ci + 15, ve).toInt();
                        }
                        else
                        {
                            content_length = 0; // no body (e.g. GET)
                        }
                    }
                }

                // If headers are done, check whether the body is complete.
                if (header_end >= 0)
                {
                    int body_have = raw.length() - (header_end + 4);
                    if (body_have >= content_length)
                        return raw;          // complete -> return now
                }
            }

            if (!client.connected() && !client.available())
                break;                        // client went away
            delay(1);                         // yield briefly, then re-check
        }
        return raw;                           // best effort on timeout
    }

    void poll()
    {
        EthernetClient eth_client = eth_server.available();

        if (eth_client)
        {
            String request_line = read_full_request(eth_client);

            Request req = parse_request(request_line);

            handle_client(eth_client, req);
            eth_client.stop();
        }

        if (wifi::connected)
        {
            WiFiClient wifi_client = wifi_server.available();

            if (wifi_client)
            {
                String request_line = read_full_request(wifi_client);
                Request req = parse_request(request_line);

                handle_client(wifi_client, req);
                wifi_client.stop();
            }
        }
    }

} // namespace web_server
