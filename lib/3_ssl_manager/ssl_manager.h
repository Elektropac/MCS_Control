#pragma once

#include <WiFi.h>
#include <Ethernet.h>
#include <ESP_SSLClient.h>

class SSLManager
{
private:
    EthernetClient ethernet_client_;
    WiFiClient wifi_client_;
    ESP_SSLClient ssl_client_;
public:
    void configure(const char *host, uint16_t port, bool use_ssl);
    ESP_SSLClient &client();
    bool isSecure() const;
};