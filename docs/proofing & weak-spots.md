---
name: "Proofing & Weak Spots"
description: "Use when creating or modifying source files. Enforces proofing and weak spot checks."
actions: "read the file, check for weak spots, and report issues"
applyTo: "lib/{0-99}_*.{cpp,h}"
---

# Weak Spots Review By Load Order

Last checked: 2026-09-15

Severity legend:
- High: immediate risk or security issue
- Mid: important but not immediately exploitable
- Low: minor correctness or maintainability issue

## 0_file_system

### lib/0_file_system/config.cpp

## 1_w5500

### lib/1_w5500/w5500.cpp

#### w5500::run_static
- Informational: the static Ethernet path still passes the gateway as the DNS address. This matches the current board/network configuration and is not considered a current issue.

#### w5500::init
- Mid: `connected` is still used as a generic initialization flag, not as a true link-health signal. This is acceptable for boot sequencing but should be separated from actual network reachability.
- Low: reset and interrupt pins exist but are not used as an active recovery or health-monitoring path.
- Recommendation: document or implement a recovery strategy if the board depends on hardware reset or interrupt handling.

## 1_wifi

### lib/1_wifi/my_wifi.cpp

#### wifi::init
- Mid: the startup routine still blocks the task for up to 10 seconds while waiting for Wi-Fi association. This is acceptable during boot but still holds the network task during a potentially long wait.
- Recommendation: move the wait into a non-blocking state machine if the task must remain responsive during startup.
- Low: the function returns early when Wi-Fi is disabled or config is missing without clearing `connected`, so a previous successful connection can remain reported as active across retries.
- Recommendation: reset `connected = false` at the start of each initialization pass before configuration checks.
- Low: the config key `enabled` is ignored. The code treats the Wi-Fi section as active whenever the object exists, so a disabled entry can still be turned on by configuration shape rather than the actual `enabled` flag.
- Recommendation: honor the explicit `enabled` field and reject disabled entries before attempting a connection.

## 3_ssl_manager

### lib/3_ssl_manager/ssl_manager.cpp

#### SSLManager::configure
- High: certificate validation is invoked, but its return value is ignored. The client continues even when validation fails.
- Recommendation: check the validation result, reject the connection on failure, and surface a clear error reason to the caller.

## 4_web_server

### lib/4_web_server/web_server.cpp

#### web_server::poll / Request parsing
- High: request handling still reads the entire client stream with `readString()` without a maximum length, timeout, or body cap. Large or slow clients can exhaust heap and block the task.
- Recommendation: enforce short read timeouts and fixed limits for the request line, headers, and body; reject oversized or incomplete requests.

#### handle_api_request
- High: the `/api` endpoint remains open to unauthenticated requests on both Ethernet and Wi-Fi listeners. There is no explicit method check before dispatch, no authorization gate, and no origin validation.
- Recommendation: require explicit access control, reject unsupported methods with `405 Method Not Allowed` before dispatch, validate the request origin where appropriate, and avoid exposing command endpoints on untrusted networks.

### lib/4_web_server/web_helpers.cpp

#### Request::Request / parse_request
- High: the parser reads everything after the header terminator as the body and ignores `Content-Length`, transfer framing, and body boundaries. It also accepts malformed header lines and does not verify the HTTP version.
- Recommendation: implement bounded HTTP framing using `Content-Length`, reject unsupported transfer encoding and malformed requests, and cap header/body sizes.

## 4_web_socket

### lib/4_web_socket/web_socket.cpp

#### web_socket::init
- Mid: reconnect logic exists, but there is still no complete reset of cached endpoint/session state when configuration is refreshed. The code reuses initialization flow without a fully explicit state-cleanup path.
- Recommendation: clear cached endpoint values and connection state before reloading or reconnecting.

#### web_socket command path
- High: incoming messages are passed directly into `function_silo::run_function_silo()` without peer authentication or authorization. The configured WebSocket path remains a command channel for any connected peer.
- Recommendation: authenticate the peer, authorize each command, and use TLS for non-local traffic.

#### web_socket::poll / message parsing
- High: `parseMessage()` detects a payload size, but the code ignores the declared size and calls `readString()` without a maximum payload cap or timeout. Large or slow peers can still exhaust heap or hold the polling task.
- Recommendation: reject messages above a fixed limit, read only the declared frame length, and enforce a timeout before deserializing JSON.

#### web_socket::poll
- Mid: reconnect handling resets `is_connected` and re-runs initialization, but the old client is not fully torn down and released before replacement. Recovery is present but not hardened against stale client state.
- Recommendation: explicitly close or release stale clients and use bounded reconnect backoff when the socket drops.

## 10_function_silo

### lib/10_function_silo/function_silo.cpp

#### run_function_silo
- High: the incoming JSON is not validated before conversion. Missing, malformed, or type-mismatched `subject` and `data` values can still reach command handling without a clear error path.
- Recommendation: validate that the payload is a JSON object, confirm `subject` is a string, and validate `data` before using it.
- High: the reboot command calls `ESP.restart()` immediately and does not wait for the caller to receive and flush an acknowledgement.
- Recommendation: acknowledge the command first and schedule the restart only after the response has been sent.
- Mid: unknown commands and missing subjects still return an empty string; HTTP and WebSocket both map this to weak or inconsistent error handling.
- Recommendation: return a uniform structured error response across both transports.

## Supporting Config

### data/config.json

#### configuration file
- High: the Wi-Fi password is still stored in plaintext and is loaded at startup. The current file also includes a real exposed credential.
- Recommendation: move credentials into protected device storage such as NVS, exclude populated config files from version control, and rotate the currently exposed secret immediately.

## Summary

The resolved items are stale configuration-load state in `config::init` and empty/invalid endpoint attempts during WebSocket initialization. The remaining high-priority risks are unauthenticated HTTP and WebSocket command execution, unbounded HTTP and WebSocket payload parsing, ignored TLS certificate validation, and plaintext Wi-Fi credentials. The next implementation priority should be access control and bounded parsing, followed by credentail migration and stronger reconnect/connection-state cleanup.

