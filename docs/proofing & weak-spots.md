---
name: "Proofing & Weak Spots"
description: "Use when creating or modifying source files. Enforces proofing and weak spot checks."
actions: "read the file, check for weak spots, and report issues"
applyTo: "lib/{0-99}_*.{cpp,h}"
---

# Weak Spots Review By Load Order

Last checked: 2026-08-24

Severity legend:
- High: immediate risk or security issue
- Mid: important but not immediately exploitable
- Low: minor correctness or maintainability issue

## 0_file_system

### lib/0_file_system/config.cpp

#### config::init
- Resolved: `config.clear()` and `is_loaded = false` now run before mount, open, and parse checks, so a failed reload cannot retain a previous successful state.

## 1_w5500

### lib/1_w5500/w5500.cpp

#### w5500::run_static
- Informational: the static Ethernet path passes the gateway as the DNS address. This is intentional for the current device/network configuration and is not considered a current issue.

#### w5500::init
- Mid: `connected` is used as a generic initialization flag, even after fallback modes or unsupported paths. It is not a true link-health signal.
- Recommendation: track initialization state separately from actual Ethernet reachability.
- Low: the reset and interrupt pins exist but are not actively used for recovery or signal monitoring.
- Recommendation: implement or document a recovery strategy if the board depends on hardware reset or interrupt handling.

## 1_wifi

### lib/1_wifi/my_wifi.cpp

#### wifi::init
- Mid: the startup routine blocks the task for up to 10 seconds waiting for Wi-Fi before continuing. That is acceptable during boot but still holds the network task during a potentially long wait.
- Recommendation: move this to a non-blocking state machine if the network task must remain responsive during startup.
- Low: the function returns early when the Wi-Fi section or credentials are missing, before resetting `connected`. A previous successful connection can therefore remain reported as connected during a retry.
- Recommendation: set `connected = false` at the start of every initialization pass, before configuration checks.

## 3_ssl_manager

### lib/3_ssl_manager/ssl_manager.cpp

#### SSLManager::configure
- High: certificate validation is called, but its result is ignored. The client may continue even if validation fails.
- Recommendation: check the return value from `validate(host, port)`, reject the connection on failure, and surface a clear error reason.

## 4_web_server

### lib/4_web_server/web_server.cpp

#### web_server::poll / Request parsing
- High: `Request` calls `client.readString()` for the entire request without a maximum size, timeout, or body limit. Large or slow clients can exhaust heap and block the task.
- Recommendation: enforce a short read timeout and hard limits for the request line, headers, and body; reject oversized or incomplete requests.

#### handle_api_request
- High: every `/api` request is routed to the command dispatcher without authentication, authorization, or origin checks. Non-`POST` requests are not rejected explicitly; they are passed an empty document and handled indirectly as a bad command. Both Ethernet and Wi-Fi listeners are exposed on port 80.
- Recommendation: require explicit access control, reject unsupported methods with `405 Method Not Allowed` before dispatch, validate the request origin where applicable, and avoid exposing command endpoints on untrusted networks.

### lib/4_web_server/web_helpers.cpp

#### Request::Request / parse_request
- High: the parser reads everything after the header terminator as the body and ignores `Content-Length`, transfer framing, and body boundaries. It also accepts malformed header lines and does not verify the HTTP version.
- Recommendation: implement bounded HTTP framing using `Content-Length`, reject unsupported transfer encoding and malformed requests, and cap header/body sizes.

## 4_web_socket

### lib/4_web_socket/web_socket.cpp

#### web_socket::load_config / web_socket::init
- Resolved in the current initialization path: endpoint strings and ports have safe defaults, completeness is checked for local and global endpoints, and `init()` returns when neither endpoint is usable instead of attempting an empty connection.
- Low: `load_config()` still does not reset cached endpoints or `try_local`/`try_global` before a subsequent reload. If configuration loading is retried after a prior successful load, stale endpoint data may survive.
- Recommendation: clear cached endpoints and reset `is_loaded`, `try_local`, `try_global`, and `is_connected` before loading configuration.

#### web_socket command path
- High: incoming messages are forwarded directly into `function_silo::run_function_silo()` without authentication or authorization. The configured local WebSocket path is therefore a command channel for any connected peer.
- Recommendation: authenticate the peer, authorize each command, and use TLS for non-local traffic.

#### web_socket::poll / message parsing
- High: `parseMessage()` supplies a message size, but the code ignores it and calls `readString()` without enforcing a maximum payload size. A large message can consume heap, and a slow peer can hold the polling task.
- Recommendation: reject messages above a fixed limit, read only the declared frame length, and apply a read timeout before deserializing JSON.

#### web_socket::poll
- Mid: the code only checks the initial `begin()` result. A disconnect or read failure does not clear `is_connected`, stop and release the client, or trigger reconnection; `sendMessage()` can continue treating a stale client as connected.
- Recommendation: detect disconnect/read failures, clear connection state, release the client, and implement bounded reconnect backoff.

## 10_function_silo

### lib/10_function_silo/function_silo.cpp

#### run_function_silo
- Mid: `json_packet["data"]` and `json_packet["data"]["value"]` are not validated before conversion. Missing or malformed values can silently set the LED incorrectly.
- Recommendation: validate that `data` is an object and `value` is a boolean before acting, and return a distinct structured error for invalid commands.
- High: `ESP.restart()` is called immediately, before the HTTP or WebSocket caller can reliably receive and flush the acknowledgment.
- Recommendation: acknowledge the command first and schedule the restart after the response is sent.
- Low: unknown commands and missing subjects return an empty string. HTTP maps this to a generic 400, while WebSocket sends an empty frame.
- Recommendation: return a uniform structured error response across both transports.

## 99_rgb

### lib/99_rgb/rgb.cpp

#### rgb::toggle
- Low: the code derives the next LED state from the pixel color but updates `isOn` independently. Those two values can drift if hardware state changes externally.
- Recommendation: use a single source of truth and route both `set()` and `toggle()` through the same state update path.

## Supporting Config

### data/config.json

#### configuration file
- High: the Wi-Fi password is stored in plaintext on the filesystem and is loaded at startup.
- Recommendation: move credentials to protected device storage such as NVS, exclude populated config files from version control, and rotate the currently exposed credential.

## Summary

The resolved items are stale configuration-load state in `config::init` and invalid endpoint attempts during normal WebSocket initialization. The remaining high-priority issues are unauthenticated HTTP and WebSocket command execution, unbounded HTTP and WebSocket message parsing, ignored TLS certificate validation, and plaintext Wi-Fi credentials. WebSocket reload handling still needs explicit state reset. The next implementation priority should be access control and bounded parsing, followed by credential migration and connection-state recovery.

