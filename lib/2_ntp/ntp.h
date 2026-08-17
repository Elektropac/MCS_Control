#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Dns.h>
#include <sys/time.h>

namespace ntp_ethernet {
    const char* server = "pool.ntp.org";
    const uint16_t port = 123;
    const uint16_t local_port = 8888;
    const uint8_t packet_size = 48;
    bool has_run = false;
    // NTP epoch starts 1900-01-01; Unix epoch starts 1970-01-01 (70 years).
    const unsigned long offset = 2208988800UL;

    EthernetUDP udp;
    byte buffer[packet_size];

    // Send one NTP request and return a Unix timestamp (0 on failure).
    static unsigned long request() {
        IPAddress server_ip;
        DNSClient dns;
        dns.begin(Ethernet.dnsServerIP());
        if (dns.getHostByName(server, server_ip) != 1) {
            Serial.println("[ntp][ethernet] DNS lookup failed");
            return 0;
        }

        memset(buffer, 0, packet_size);
        buffer[0] = 0b11100011; // LI=3, Version=4, Mode=3 (client)
        buffer[1] = 0;          // Stratum: unspecified
        buffer[2] = 6;          // Polling interval
        buffer[3] = 0xEC;       // Peer clock precision

        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000) {
            if (udp.parsePacket() >= packet_size) {
                udp.read(buffer, packet_size);
                // Transmit timestamp is at bytes 40-43
                unsigned long t = ((unsigned long)buffer[40] << 24) |
                                  ((unsigned long)buffer[41] << 16) |
                                  ((unsigned long)buffer[42] <<  8) |
                                   (unsigned long)buffer[43];
                return t - offset;
            }
            delay(10);
        }
        return 0;
    }

    // Sync the ESP32 system clock from NTP. Must be called after Ethernet is up.
    // Retries up to 'retries' times with a 2-second gap between attempts.
    void sync(uint8_t retries = 5) {
        if (has_run) {
            Serial.println("[ntp][ethernet] Sync already executed once, skipping.");
            return;
        }
        has_run = true;

        Serial.println("[ntp][ethernet] Syncing time...");
        udp.begin(local_port);

        for (uint8_t i = 0; i < retries; i++) {
            unsigned long t = request();
            if (t > 1000000000UL) { // sanity: after year 2001
                timeval tv = { (time_t)t, 0 };
                settimeofday(&tv, nullptr);
                Serial.print("[ntp][ethernet] Time synced: ");
                Serial.println(t);
                udp.stop();
                return;
            }
            Serial.printf("[ntp][ethernet] Attempt %u failed, retrying...\n", i + 1);
            delay(2000);
        }

        Serial.println("[ntp][ethernet] Failed to sync time after retries.");
        udp.stop();
    }
}

namespace ntp_wifi {
    const char* server = "pool.ntp.org";
    const uint16_t port = 123;
    const uint16_t local_port = 8889;
    const uint8_t packet_size = 48;
    bool has_run = false;
    // NTP epoch starts 1900-01-01; Unix epoch starts 1970-01-01 (70 years).
    const unsigned long offset = 2208988800UL;

    WiFiUDP udp;
    byte buffer[packet_size];

    // Send one NTP request and return a Unix timestamp (0 on failure).
    static unsigned long request() {
        IPAddress server_ip;
        if (WiFi.hostByName(server, server_ip) != 1) {
            Serial.println("[ntp][wifi] DNS lookup failed");
            return 0;
        }

        memset(buffer, 0, packet_size);
        buffer[0] = 0b11100011; // LI=3, Version=4, Mode=3 (client)
        buffer[1] = 0;          // Stratum: unspecified
        buffer[2] = 6;          // Polling interval
        buffer[3] = 0xEC;       // Peer clock precision

        udp.beginPacket(server_ip, port);
        udp.write(buffer, packet_size);
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000) {
            if (udp.parsePacket() >= packet_size) {
                udp.read(buffer, packet_size);
                // Transmit timestamp is at bytes 40-43
                unsigned long t = ((unsigned long)buffer[40] << 24) |
                                  ((unsigned long)buffer[41] << 16) |
                                  ((unsigned long)buffer[42] <<  8) |
                                   (unsigned long)buffer[43];
                return t - offset;
            }
            delay(10);
        }
        return 0;
    }

    // Sync the ESP32 system clock from NTP. Must be called after WiFi is up.
    // Retries up to 'retries' times with a 2-second gap between attempts.
    void sync(uint8_t retries = 5) {
        if (has_run) {
            Serial.println("[ntp][wifi] Sync already executed once, skipping.");
            return;
        }
        has_run = true;

        Serial.println("[ntp][wifi] Syncing time...");
        udp.begin(local_port);

        for (uint8_t i = 0; i < retries; i++) {
            unsigned long t = request();
            if (t > 1000000000UL) { // sanity: after year 2001
                timeval tv = { (time_t)t, 0 };
                settimeofday(&tv, nullptr);
                Serial.print("[ntp][wifi] Time synced: ");
                Serial.println(t);
                udp.stop();
                return;
            }
            Serial.printf("[ntp][wifi] Attempt %u failed, retrying...\n", i + 1);
            delay(2000);
        }

        Serial.println("[ntp][wifi] Failed to sync time after retries.");
        udp.stop();
    }
}
