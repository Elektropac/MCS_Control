#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>

struct Request
{
    String method;
    String path;
    JsonDocument headers;
    JsonDocument body;
};

Request parse_request(String raw);
void handle_api_request(Client &client, Request &req);
void handle_page_request(Client &client, Request &req);