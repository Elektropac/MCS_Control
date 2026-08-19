#include "web_helpers.h"

#include "function_silo.h"
#include "logging.h"
#include "web_files.h"

Request parse_request(String raw)
{
    Request req;
    int firstLineEnd = raw.indexOf("\r\n");
    if (firstLineEnd == -1) return req;

    String requestLine = raw.substring(0, firstLineEnd);
    int methodEnd = requestLine.indexOf(' ');
    int pathEnd = requestLine.indexOf(' ', methodEnd + 1);
    if (methodEnd == -1 || pathEnd == -1) return req;

    req.method = requestLine.substring(0, methodEnd);
    req.path = requestLine.substring(methodEnd + 1, pathEnd);
    int headersEnd = raw.indexOf("\r\n\r\n");
    if (headersEnd == -1) return req;

    int pos = firstLineEnd + 2;
    while (pos < headersEnd)
    {
        int lineEnd = raw.indexOf("\r\n", pos);
        if (lineEnd == -1 || lineEnd > headersEnd) lineEnd = headersEnd;
        String line = raw.substring(pos, lineEnd);
        int colon = line.indexOf(':');
        if (colon != -1)
        {
            String key = line.substring(0, colon);
            String value = line.substring(colon + 1);
            key.trim();
            value.trim();
            req.headers[key] = value;
        }
        pos = lineEnd + 2;
    }

    String bodyString = raw.substring(headersEnd + 4);
    bodyString.trim();
    if (bodyString.length() > 0)
    {
        DeserializationError error = deserializeJson(req.body, bodyString);
        if (error) log_error("[web_server] Failed to parse JSON body: %s", error.c_str());
    }
    return req;
}

void handle_api_request(Client &client, Request &req)
{
    String result = function_silo::run_function_silo(req.body);
    if (result.length() == 0)
    {
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("Bad Request: No result from function silo.");
        return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print(result);
}

void handle_page_request(Client &client, Request &req)
{
    EmbeddedWebFile file;
    bool file_found = false;
    for (size_t i = 0; i < WEB_FILES_COUNT; ++i)
    {
        if (req.path == WEB_FILES[i].path)
        {
            file = WEB_FILES[i];
            file_found = true;
            break;
        }
    }

    if (!file_found)
    {
        client.println("HTTP/1.1 404 Not Found");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("404 Not Found: The requested resource was not found on this server.");
        return;
    }

    size_t file_size = static_cast<size_t>(reinterpret_cast<uintptr_t>(file.size));
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: " + String(file.content_type));
    client.println("Content-Length: " + String(file_size));
    if (file.gzip) client.println("Content-Encoding: gzip");
    client.println("Connection: close");
    client.println();

    uint8_t buffer[1024];
    size_t bytes_remaining = file_size;
    const uint8_t *file_ptr = file.start;
    while (bytes_remaining > 0)
    {
        size_t bytes_to_send = (bytes_remaining < sizeof(buffer)) ? bytes_remaining : sizeof(buffer);
        memcpy(buffer, file_ptr, bytes_to_send);
        client.write(buffer, bytes_to_send);
        file_ptr += bytes_to_send;
        bytes_remaining -= bytes_to_send;
    }
}
