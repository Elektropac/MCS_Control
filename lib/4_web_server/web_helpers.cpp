#include "web_helpers.h"

#include "function_silo.h"
#include "logging.h"
#include "web_files.h"

// Read a full HTTP request without blocking for the stream's ~1s timeout.
// readString() always stalls until the inter-byte timeout elapses (~1000ms
// on every request). Instead we read bytes as they arrive and return as soon
// as the request is complete: headers terminator (\r\n\r\n) seen AND the body
// (per Content-Length) fully received. A short wall-clock cap guards against a
// stalled/partial client. (Control 0.3.34 non-blocking request fix.)
static String read_full_request(Client &client)
{
    String raw;
    raw.reserve(512);
    const uint32_t start = millis();
    const uint32_t MAX_MS = 250;          // hard cap per request
    int header_end = -1;
    int content_length = -1;

    while (millis() - start < MAX_MS)
    {
        while (client.available())
        {
            raw += (char)client.read();

            // Detect end of headers once.
            if (header_end < 0)
            {
                header_end = raw.indexOf("\r\n\r\n");
                if (header_end >= 0)
                {
                    // Parse Content-Length (case-insensitive).
                    String h = raw.substring(0, header_end);
                    h.toLowerCase();
                    int ci = h.indexOf("content-length:");
                    if (ci >= 0)
                    {
                        int ve = h.indexOf("\r\n", ci);
                        if (ve < 0) ve = h.length();
                        content_length = h.substring(ci + 15, ve).toInt();
                    }
                    else
                    {
                        content_length = 0; // no body (e.g. GET)
                    }
                }
            }

            // If headers are done, check whether the body is complete.
            if (header_end >= 0)
            {
                int body_have = raw.length() - (header_end + 4);
                if (body_have >= content_length)
                    return raw;          // complete -> return now
            }
        }

        if (!client.connected() && !client.available())
            break;                        // client went away
        delay(1);                         // yield briefly, then re-check
    }
    return raw;                           // best effort on timeout
}

Request::Request(Client &client)
{
    String raw = read_full_request(client);

    int firstLineEnd = raw.indexOf("\r\n");
    if (firstLineEnd == -1) return;

    String requestLine = raw.substring(0, firstLineEnd);
    int methodEnd = requestLine.indexOf(' ');
    int pathEnd = requestLine.indexOf(' ', methodEnd + 1);
    if (methodEnd == -1 || pathEnd == -1) return;

    method = requestLine.substring(0, methodEnd);
    path = requestLine.substring(methodEnd + 1, pathEnd);

    int headersEnd = raw.indexOf("\r\n\r\n");
    if (headersEnd == -1) return;

    raw_headers = raw.substring(firstLineEnd + 2, headersEnd);
    raw_body = raw.substring(headersEnd + 4);
    raw_body.trim();
}

JsonDocument Request::get_headers() const
{
    JsonDocument headers;
    int pos = 0;
    while (pos < static_cast<int>(raw_headers.length()))
    {
        int lineEnd = raw_headers.indexOf("\r\n", pos);
        if (lineEnd == -1) lineEnd = raw_headers.length();
        String line = raw_headers.substring(pos, lineEnd);
        int colon = line.indexOf(':');
        if (colon != -1)
        {
            String key = line.substring(0, colon);
            String value = line.substring(colon + 1);
            key.trim();
            value.trim();
            headers[key] = value;
        }
        pos = lineEnd + 2;
    }
    return headers;
}

JsonDocument Request::get_body() const
{
    JsonDocument body;
    if (raw_body.length() > 0)
    {
        DeserializationError error = deserializeJson(body, raw_body);
        if (error) log_error("[web_server] Failed to parse JSON body: %s", error.c_str());
    }
    return body;
}

void handle_api_request(Client &client, Request &req)
{
    JsonDocument data;

    if (req.method == "POST") {
        data = req.get_body();
    }

    String result = function_silo::run_function_silo(data);
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
    client.println("Content-Length: " + String(result.length()));
    client.println("Connection: close");
    client.println();

    // Send in chunks — W5500 TX buffer is only 2048 bytes, so a single
    // client.print() silently truncates large payloads (e.g. selftest JSON).
    const size_t CHUNK = 1024;
    const char* ptr = result.c_str();
    size_t remaining = result.length();
    while (remaining > 0)
    {
        size_t n = (remaining < CHUNK) ? remaining : CHUNK;
        client.write((const uint8_t*)ptr, n);
        ptr += n;
        remaining -= n;
    }
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
