#include "ntp.h"

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Dns.h>
#include <sys/time.h>

#include "logging.h"

namespace
{
    constexpr const char *server = "pool.ntp.org";
    constexpr uint16_t port = 123;
    constexpr uint8_t packet_size = 48;
    constexpr unsigned long epoch_offset = 2208988800UL;

    unsigned long read_timestamp(uint8_t *buffer)
    {
        return ((unsigned long)buffer[40] << 24) |
               ((unsigned long)buffer[41] << 16) |
               ((unsigned long)buffer[42] << 8) |
               (unsigned long)buffer[43];
    }

    void prepare_packet(uint8_t *buffer)
    {
        memset(buffer, 0, packet_size);
        buffer[0] = 0b11100011;
        buffer[2] = 6;
        buffer[3] = 0xEC;
    }
}

namespace ntp_ethernet
{
    bool has_run = false;
    EthernetUDP udp;
    uint8_t buffer[packet_size];

    static unsigned long request()
    {
        IPAddress server_ip;
        DNSClient dns;
        dns.begin(Ethernet.dnsServerIP());
        if (dns.getHostByName(server, server_ip) != 1)
        {
            log_error("[ntp][ethernet] DNS lookup failed");
            return 0;
        }

        prepare_packet(buffer);
        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            if (udp.parsePacket() >= packet_size)
            {
                udp.read(buffer, packet_size);
                return read_timestamp(buffer) - epoch_offset;
            }
            delay(10);
        }
        return 0;
    }

    void sync(uint8_t retries)
    {
        if (has_run)
        {
            log_info("[ntp][ethernet] Sync already executed once, skipping.");
            return;
        }
        has_run = true;
        log_info("[ntp][ethernet] Syncing time...");
        udp.begin(8888);
        for (uint8_t i = 0; i < retries; i++)
        {
            unsigned long timestamp = request();
            if (timestamp > 1000000000UL)
            {
                timeval tv = {(time_t)timestamp, 0};
                settimeofday(&tv, nullptr);
                log_info("[ntp][ethernet] Time synced: %lu", timestamp);
                udp.stop();
                return;
            }
            log_info("[ntp][ethernet] Attempt %u failed, retrying...\n", i + 1);
            delay(2000);
        }
        log_info("[ntp][ethernet] Failed to sync time after retries.");
        udp.stop();
    }
}

namespace ntp_wifi
{
    bool has_run = false;
    WiFiUDP udp;
    uint8_t buffer[packet_size];

    static unsigned long request()
    {
        IPAddress server_ip;
        if (WiFi.hostByName(server, server_ip) != 1)
        {
            log_error("[ntp][wifi] DNS lookup failed");
            return 0;
        }

        prepare_packet(buffer);
        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();
        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            if (udp.parsePacket() >= packet_size)
            {
                udp.read(buffer, packet_size);
                return read_timestamp(buffer) - epoch_offset;
            }
            delay(10);
        }
        return 0;
    }

    void sync(uint8_t retries)
    {
        if (has_run)
        {
            log_info("[ntp][wifi] Sync already executed once, skipping.");
            return;
        }
        has_run = true;
        log_info("[ntp][wifi] Syncing time...");
        udp.begin(8889);
        for (uint8_t i = 0; i < retries; i++)
        {
            unsigned long timestamp = request();
            if (timestamp > 1000000000UL)
            {
                timeval tv = {(time_t)timestamp, 0};
                settimeofday(&tv, nullptr);
                log_info("[ntp][wifi] Time synced: %lu", timestamp);
                udp.stop();
                return;
            }
            log_info("[ntp][wifi] Attempt %u failed, retrying...\n", i + 1);
            delay(2000);
        }
        log_error("[ntp][wifi] Failed to sync time after retries.");
        udp.stop();
    }
}
