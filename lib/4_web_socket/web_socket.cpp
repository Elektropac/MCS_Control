#include "web_socket.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "function_silo.h"
#include "logging.h"
#include "ssl_manager.h"
#include "my_wifi.h"

namespace web_socket
{
    bool is_connected = false;
    bool is_secure = false;
    SSLManager ssl_manager;
    WebSocketClient *web_socket_client = nullptr;

    String local_host = "";
    int local_port = 0;
    String local_path = "";

    String global_host = "";
    int global_port = 0;
    String global_path = "";

    bool is_loaded = false;
    bool try_local = false;
    bool try_global = false;

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

    void load_config()
    {
        auto is_server_config = config::config["connection"]["server"].isNull();
        if (is_server_config)
        {
            log_error("[web_socket] WebSocket server configuration not found in config. WebSocket will not be initialized.");
            return;
        }

        log_info("[web_socket] Setting up WebSocket client.");
        auto server_config = config::config["connection"]["server"].as<JsonObject>();

        auto is_local_host = server_config["local"]["host"].isNull();
        auto is_local_port = server_config["local"]["port"].isNull();

        if (!(is_local_host || is_local_port))
        {
            local_host = server_config["local"]["host"].as<String>();
            local_port = server_config["local"]["port"].as<int>();

            try_local = !local_host.isEmpty() && local_port > 0;
        }

        auto is_global_host = server_config["global"]["host"].isNull();
        auto is_global_port = server_config["global"]["port"].isNull();

        if (!(is_global_host || is_global_port))
        {
            global_host = server_config["global"]["host"].as<String>();
            global_port = server_config["global"]["port"].as<int>();

            try_global = !global_host.isEmpty() && global_port > 0;
        }

        if (!try_local && !try_global)
        {
            log_error("[web_socket] No complete WebSocket server configuration found. WebSocket will not be initialized.");
            return;
        }

        is_loaded = true;
    }

    void init()
    {
        if (is_loaded == false)
        {
            load_config();
        }

        if (!is_loaded)
        {
            return;
        }

        if (try_local)
        {
            log_info("[web_socket] Attempting to connect to local WebSocket");
            is_connected = run(local_host, local_port);

            if (!is_connected)
            {
                if (try_global)
                {
                    log_error("[web_socket] Failed to connect to local WebSocket server. Attempting to connect to global WebSocket server.");
                    is_connected = run(global_host, global_port);
                }
            }
        }
        else if (try_global)
        {
            log_info("[web_socket] Attempting to connect to global WebSocket server");
            is_connected = run(global_host, global_port);
        }
    }

    void sendMessage(const String &message)
    {
        if (is_connected && web_socket_client)
        {
            web_socket_client->beginMessage(TYPE_TEXT);
            web_socket_client->write((const uint8_t *)message.c_str(), message.length());
            web_socket_client->endMessage();
        }
        else
            log_error("[web_socket] WebSocket is not connected. Cannot send message.");
    }

    int lastMillis = 0;

    void poll()
    {
        if (web_socket_client && is_connected)
        {
            int message_size = web_socket_client->parseMessage();
            if (message_size > 0)
            {
                String data_string = web_socket_client->readString();
                JsonDocument data;
                DeserializationError error = deserializeJson(data, data_string);
                if (error)
                {
                    log_error("[web_socket] Failed to parse JSON: %s", error.c_str());
                    log_error("[web_socket] Received data: %s", data_string.c_str());
                    return;
                }
                sendMessage(function_silo::run_function_silo(data));
            }
        }

        if (is_connected && (millis() - lastMillis > 5000))
        {
            lastMillis = millis();
            JsonDocument ping_doc;
            ping_doc["subject"] = "ping";
            ping_doc["data"]["timestamp"] = lastMillis;
            String ping_string;
            serializeJson(ping_doc, ping_string);
            sendMessage(ping_string);
        }
    }

}
