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

    void prepare_packet(uint8_t *buffer, uint32_t transmit_timestamp)
    {
        memset(buffer, 0, packet_size);
        buffer[0] = 0b11100011;
        buffer[2] = 6;
        buffer[3] = 0xEC;
        buffer[40] = (uint8_t)(transmit_timestamp >> 24);
        buffer[41] = (uint8_t)(transmit_timestamp >> 16);
        buffer[42] = (uint8_t)(transmit_timestamp >> 8);
        buffer[43] = (uint8_t)transmit_timestamp;
    }

    // Rejects packets that aren't a genuine, unsynchronized-free server reply to our own request.
    bool validate_response(uint8_t *buffer, int len, IPAddress remote_ip, IPAddress expected_ip,
                            uint16_t remote_port, uint32_t sent_transmit_timestamp)
    {
        if (len < (int)packet_size)
            return false;
        if (remote_ip != expected_ip || remote_port != port)
            return false;

        uint8_t leap_indicator = (buffer[0] >> 6) & 0x03;
        if (leap_indicator == 3) // unsynchronized/alarm condition
            return false;

        uint8_t mode = buffer[0] & 0x07;
        if (mode != 4) // 4 = server reply
            return false;

        uint8_t stratum = buffer[1];
        if (stratum == 0 || stratum > 15) // 0 = kiss-of-death, >15 invalid
            return false;

        unsigned long origin_timestamp = ((unsigned long)buffer[24] << 24) |
                                          ((unsigned long)buffer[25] << 16) |
                                          ((unsigned long)buffer[26] << 8) |
                                          (unsigned long)buffer[27];
        if (origin_timestamp != sent_transmit_timestamp)
            return false;

        return true;
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

        uint32_t transmit_timestamp = (uint32_t)micros();
        prepare_packet(buffer, transmit_timestamp);
        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            int len = udp.parsePacket();
            if (len >= packet_size)
            {
                udp.read(buffer, packet_size);
                if (!validate_response(buffer, len, udp.remoteIP(), server_ip, udp.remotePort(), transmit_timestamp))
                {
                    log_error("[ntp][ethernet] Ignoring invalid NTP response");
                    continue;
                }
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
                has_run = true;
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

        uint32_t transmit_timestamp = (uint32_t)micros();
        prepare_packet(buffer, transmit_timestamp);
        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();
        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            int len = udp.parsePacket();
            if (len >= packet_size)
            {
                udp.read(buffer, packet_size);
                if (!validate_response(buffer, len, udp.remoteIP(), server_ip, udp.remotePort(), transmit_timestamp))
                {
                    log_error("[ntp][wifi] Ignoring invalid NTP response");
                    continue;
                }
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
                has_run = true;
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
