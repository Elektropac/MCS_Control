#include "ssl_manager.h"

#include "config.h"
#include "my_wifi.h"
#include "ntp.h"
#include "w5500.h"
#include "root_ca.h"

String SSLManager::find_mode()
{
    auto is_connect_client = config::config["connection"]["internet_client"].isNull();
    if (is_connect_client)
    {
        return "exit";
    }

    auto mode = config::config["connection"]["internet_client"].as<String>();
    if (mode == "ethernet" && !w5500::connected)
    {
        return "exit";
    }
    if (mode == "wifi" && !wifi::connected)
    {
        return "exit";
    }
    return mode;
}

void SSLManager::configure(const char *host, uint16_t port, bool use_ssl)
{
    auto mode = find_mode();
    if (mode == "exit")
    {
        return;
    }
    if (mode == "ethernet")
    {
        ssl_client_.setClient(&ethernet_client_, use_ssl);
        if (use_ssl) ntp_ethernet::sync();
    }
    else if (mode == "wifi")
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