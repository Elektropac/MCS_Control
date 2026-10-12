#include "ssl_manager.h"

#include "config.h"
#include "my_wifi.h"
#include "ntp.h"
#include "w5500.h"
#include "root_ca.h"


void SSLManager::configure(const char *host, uint16_t port, bool use_ssl)
{
    if (config::internet_client == "ethernet")
    {
        ssl_client_.setClient(&ethernet_client_, use_ssl);
        if (use_ssl) ntp_ethernet::sync();
    }
    else if (config::internet_client == "wifi")
    {
        ssl_client_.setClient(&wifi_client_, use_ssl);
        if (use_ssl) ntp_wifi::sync();
    }

    if (use_ssl)
    {
        ssl_client_.setCACert(k_default_root_ca);
        ssl_client_.validate(host, port);
        ssl_client_.setBufferSizes(2048, 2048);
    }
}

ESP_SSLClient &SSLManager::client()
{
    return ssl_client_;
}

bool SSLManager::isSecure() const
{
    return ssl_client_.isSecure();
}