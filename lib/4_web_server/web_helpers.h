#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>

class Request
{
public:
    String method;
    String path;
    String raw_headers;
    String raw_body;

    explicit Request(Client &client);

    JsonDocument get_headers() const;
    JsonDocument get_body() const;
};

void handle_api_request(Client &client, Request &req);
void handle_page_request(Client &client, Request &req);