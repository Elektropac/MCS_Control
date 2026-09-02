#include "web_socket.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "logging.h"
#include "config.h"

#include "ssl_manager.h"

#include "my_wifi.h"
#include "w5500.h"

#include "function_silo.h"

namespace web_socket
{
    bool is_connected = false;
    bool has_run = false;

    bool is_secure = false;
    SSLManager ssl_manager;
    WebSocketClient *web_socket_client = nullptr;

    bool run(String host, int port)
    {
        if (web_socket_client != nullptr)
        {
            web_socket_client->stop();
            delete web_socket_client;
            web_socket_client = nullptr;
        }

        bool use_ssl = (port == 443);

        web_socket_client = new WebSocketClient(ssl_manager.client(), host.c_str(), port);
        ssl_manager.configure(host.c_str(), port, use_ssl);

        String path = "/ws/" + wifi::make_mac(); // Default path

        log_info("[web_socket] WebSocket connecting to %s:%d%s", host.c_str(), port, path.c_str());

        auto init_result = web_socket_client->begin(path.c_str());
        is_secure = ssl_manager.isSecure();

        if (init_result == 0)
        {
            log_info("[web_socket] WebSocket connected.");
            log_info("[web_socket] Secure connection: %s", is_secure ? "Yes" : "No");
            return true;
        }

        log_error("[web_socket] WebSocket connection failed.");

        return false;
    }

    void init()
    {
        if (config::internet_client == "ethernet" && config::ethernet_config.mode == "local-link") {
            return;
        }
        if (config::internet_client == "ethernet" && config::ethernet_config.mode == "dhcp" && w5500::connected == false) {
            return;
        }
        if (config::internet_client == "wifi" && wifi::connected == false) {
            return;
        }

        if (config::server_config.try_local)
        {
            log_info("[web_socket] Attempting to connect to local WebSocket");
            is_connected = run(config::server_config.local_host, config::server_config.local_port);

            if (!is_connected && config::server_config.global_host.length() > 0)
            {
                log_error("[web_socket] Failed to connect to local WebSocket server. Attempting to connect to global WebSocket server.");
                is_connected = run(config::server_config.global_host, config::server_config.global_port);
            }
        }
        else if (config::server_config.global_host.length() > 0)
        {
            log_info("[web_socket] Attempting to connect to global WebSocket server");
            is_connected = run(config::server_config.global_host, config::server_config.global_port);
        }

        has_run = true;
    }

    int lastMessageMillis = 0;

    void sendMessage(const String &message)
    {
        if (is_connected && web_socket_client)
        {
            lastMessageMillis = millis();

            web_socket_client->beginMessage(TYPE_TEXT);
            web_socket_client->write((const uint8_t *)message.c_str(), message.length());
            web_socket_client->endMessage();
        }
        else
            log_error("[web_socket] WebSocket is not connected. Cannot send message.");
    }

    void poll()
    {
        if (web_socket_client && is_connected)
        {
            if (millis() - lastMessageMillis > 1000 * 60) // Send a ping if no other messages have been sent for 1 minute
            {
                JsonDocument ping_doc;
                ping_doc["subject"] = "ping";
                ping_doc["data"]["timestamp"] = millis();
                String ping_string;
                serializeJson(ping_doc, ping_string);
                sendMessage(ping_string);
            }

            int message_size = web_socket_client->parseMessage();
            if (message_size > 0)
            {
                lastMessageMillis = millis();
                
                String data_string = web_socket_client->readString();
                JsonDocument data;
                DeserializationError error = deserializeJson(data, data_string);
                if (error)
                {
                    log_error("[web_socket] Failed to parse JSON: %s", error.c_str());
                    log_error("[web_socket] Received data: %s", data_string.c_str());
                }
                else
                {
                    // handle valid JSON data
                    String l;
                    serializeJsonPretty(data, l);
                    log_info("[web_socket] Received JSON data: \n%s", l.c_str());
                }

            }

            // handle if gets disconnected
            if (!web_socket_client->connected())
            {
                log_error("[web_socket] WebSocket disconnected.");
                is_connected = false;
            }
        }
        else if (is_connected == false && has_run) {
            init();
        }
    }
}
