#include "web_socket.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

#include "logging.h"
#include "config.h"

#include "ssl_manager.h"

#include "my_wifi.h"
#include "w5500.h"
#include "queues.h"

#include "function_silo.h"

String make_key()
{
    String secret = "my_secret"; // Replace with your actual secret
    String mac = wifi::make_mac();
    String key = mac + secret;

    unsigned char hash[32];

    mbedtls_sha256_ret((const unsigned char *)key.c_str(), key.length(), hash, 0);

    String result = "";
    for (int i = 0; i < 32; i++)
    {
        char buf[3];
        sprintf(buf, "%02x", hash[i]);
        result += buf;
    }

    return result;
}

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

        // path = /ws/<mac_address> + ?key=<generated_key> + &config_version=<config_version>
        String path = "/control/ws/" + wifi::make_mac() + "?key=" + make_key() + "&config_version=" + config::config_version;

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
        if (config::internet_client == "ethernet" && config::ethernet_config.mode == "local-link")
        {
            return;
        }
        if (config::internet_client == "ethernet" && config::ethernet_config.mode == "dhcp" && w5500::connected == false)
        {
            return;
        }
        if (config::internet_client == "wifi" && wifi::connected == false)
        {
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

        if (is_connected)
        {
            has_run = true;
        }
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
            // handle reconnection if the WebSocket gets disconnected
            if (!web_socket_client->connected() && has_run)
            {
                log_error("[web_socket] WebSocket disconnected.");
                is_connected = false;

                init();
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
                    String result = function_silo::run_function_silo(data);
                    if (result.length() > 0)
                    {
                        sendMessage(result);
                    }
                }
            }

            if (millis() - lastMessageMillis > 1000 * 60) // Send a ping if no other messages have been sent for 1 minute
            {
                JsonDocument ping_doc;
                ping_doc["subject"] = "ping";
                ping_doc["data"]["timestamp"] = millis();
                String ping_string;
                serializeJson(ping_doc, ping_string);
                sendMessage(ping_string);
            }

            QueueSetMemberHandle_t activeQueue = xQueueSelectFromSet(queues::queues_set, portMAX_DELAY);

            if (activeQueue == queues::probe_queue) {
                queues::ProbeSample probe_sample;
                if (xQueueReceive(queues::probe_queue, &probe_sample, 0) == pdTRUE) {
                    JsonDocument probe_doc;
                    probe_doc["subject"] = "probe/" + probe_sample.id;
                    probe_doc["data"]["ma"] = probe_sample.ma;
                    probe_doc["data"]["mm"] = probe_sample.mm;
                    probe_doc["data"]["timestamp"] = millis();

                    String probe_string;
                    serializeJson(probe_doc, probe_string);
                    sendMessage(probe_string);

                    // Process the probe sample here
                }
            }
            else if (activeQueue == queues::pump_queue) {
                queues::PumpSample pump_sample;
                if (xQueueReceive(queues::pump_queue, &pump_sample, 0) == pdTRUE) {
                    // Process the pump sample here
                }
            }







        }
    }
}
