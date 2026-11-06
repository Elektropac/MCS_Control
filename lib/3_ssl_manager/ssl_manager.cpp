#include "ssl_manager.h"

#include <esp_heap_caps.h>
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
        // TLS (BearSSL) needs a sizeable, reasonably contiguous heap for the
        // CA cert + 2x2KB record buffers. If the heap is too low (e.g. this
        // board has no working PSRAM), BearSSL's allocator calls abort() deep
        // inside setCACert()/validate() and HARD-RESETS the whole device.
        // Guard against that: if there isn't enough headroom, skip the secure
        // setup and leave the client non-secure. web_socket::run() sees
        // isSecure()==false and fails gracefully, then retries later — the
        // device stays up instead of crash-looping when the cloud/TLS can't be
        // brought up.
        const size_t needed = 40000;   // conservative headroom for cert+buffers
        size_t freeHeap = ESP.getFreeHeap();
        size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (freeHeap < needed || largestBlock < needed)
        {
            log_error("[ssl] Not enough heap for TLS (free=%u, largest=%u, need=%u) "
                      "- skipping secure setup to avoid an abort()/reset.",
                      (unsigned)freeHeap, (unsigned)largestBlock, (unsigned)needed);
            return;   // leaves ssl_client_ non-secure; run() handles it
        }

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