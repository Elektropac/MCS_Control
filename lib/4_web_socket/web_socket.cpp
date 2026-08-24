#include "web_socket.h"

#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "function_silo.h"
#include "logging.h"
#include "ssl_manager.h"

namespace web_socket
{
    bool is_connected = false;
    bool is_secure = false;
    SSLManager ssl_manager;
    WebSocketClient *web_socket_client = nullptr;

    bool run(String host, int port, String path)
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
        auto is_local_path = server_config["local"]["path"].isNull();
        if (!(is_local_host || is_local_port || is_local_path))
        {
            auto local_host = server_config["local"]["host"].as<String>();
            auto local_port = server_config["local"]["port"].as<int>();
            auto local_path = server_config["local"]["path"].as<String>();
            log_info("[web_socket] Connecting to local WebSocket server: %s:%d%s", local_host.c_str(), local_port, local_path.c_str());
            is_connected = run(local_host, local_port, local_path);
        }
        else
        {
            log_error("[web_socket] WebSocket local server configuration is incomplete. WebSocket will try to use global server configuration.");
        }
        if (is_connected) return;

        auto is_global_host = server_config["global"]["host"].isNull();
        auto is_global_port = server_config["global"]["port"].isNull();
        auto is_global_path = server_config["global"]["path"].isNull();
        if (is_global_host || is_global_port || is_global_path)
        {
            log_error("[web_socket] WebSocket global server configuration is incomplete. WebSocket will not be initialized.");
            return;
        }

        auto global_host = server_config["global"]["host"].as<String>();
        auto global_port = server_config["global"]["port"].as<int>();
        auto global_path = server_config["global"]["path"].as<String>();
        log_info("[web_socket] Connecting to global WebSocket server: %s:%d%s", global_host.c_str(), global_port, global_path.c_str());
        is_connected = run(global_host, global_port, global_path);
    }

    void sendMessage(const String &message)
    {
        if (is_connected && web_socket_client)
        {
            web_socket_client->beginMessage(TYPE_TEXT);
            web_socket_client->write((const uint8_t *)message.c_str(), message.length());
            web_socket_client->endMessage();
        }
        else log_error("[web_socket] WebSocket is not connected. Cannot send message.");
    }

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
    }
}
