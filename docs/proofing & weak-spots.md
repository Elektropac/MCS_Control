---
name: "Proofing & Weak Spots"
description: "Use when creating or modifying source files. Enforces proofing and weak spot checks."
actions: "read the file, check for weak spots, and report issues"
applyTo: "lib/{0-99}_*.{cpp,h}"
---

# Weak Spots Review By Load Order

Severity legend:
- High: immediate risk or security issue
- Mid: important but not immediately exploitable
- Low: minor correctness or maintainability issue

## 0_file_system

### lib/0_file_system/config.h

#### config::init
- Mid: the config object is cleared at the start of the load, but `is_loaded` is not explicitly reset before early returns, so a previous successful load can survive a failed open or failed parse.
- Recommendation: clear the document and loaded flag before every open/parse cycle so stale state cannot survive a failed load.

## 1_w5500

### lib/1_w5500/w5500.h

#### w5500::run_static
- Mid: the static Ethernet path still passes the gateway IP as both the gateway and DNS values. This can break hostname resolution if the gateway is not also a DNS server. // note this is not a problem
- Recommendation: add an explicit DNS field or document a known fallback.

#### w5500::init
- Mid: `connected` is used as a generic initialization flag, even after fallback modes or unsupported paths. It is not a true link-health signal.
- Recommendation: track initialization state separately from actual Ethernet reachability.
- Low: the reset and interrupt pins exist but are not actively used for recovery or signal monitoring.
- Recommendation: implement or document a recovery strategy if the board depends on hardware reset or interrupt handling.

## 1_wifi

### lib/1_wifi/my_wifi.h

#### wifi::init
- Mid: the startup routine blocks the task for up to 10 seconds waiting for Wi-Fi before continuing. That is acceptable during boot but still holds the network task during a potentially long wait.
- Recommendation: move this to a non-blocking state machine if the network task must remain responsive during startup.
- Low: the function returns early on missing Wi-Fi config without explicitly resetting `connected` at entry. A stale value may remain across retries.
- Recommendation: set `connected = false` at the start of every retry or initialization pass.

## 3_ssl_manager

### lib/3_ssl_manager/ssl_manager.h

#### SSLManager::configure
- High: certificate validation is called, but its result is ignored. The client may continue even if validation fails.
- Recommendation: treat validation failure as a connection failure and surface a clear error reason.

## 4_web_server

### lib/4_web_server/web_server.h

#### web_server::poll / Request parsing
- High: `client.readString()` reads the entire request without a maximum size, timeout, or body limit. Large or slow clients can exhaust heap and block the task.
- Recommendation: enforce hard limits on request length and use bounded request parsing.

#### handle_api_request
- High: every `/api` request is routed to the command dispatcher without authentication, authorization, origin checks, or method enforcement.
- Recommendation: require explicit access control and allow only the required HTTP methods before processing commands.

### lib/4_web_server/web_helpers.h

#### Request::Request / parse_request
- Mid: the parser reads everything after the header terminator as the body and ignores `Content-Length`, transfer framing, and body boundaries.
- Recommendation: implement bounded HTTP framing and reject malformed or oversized bodies.

## 4_web_socket

### lib/4_web_socket/web_socket.h

#### web_socket command path
- High: incoming messages are forwarded directly into `function_silo::run_function_silo()` without authentication or authorization.
- Recommendation: require peer authentication and authorization before any command is accepted.

#### web_socket::poll
- Mid: the code only checks the initial `begin()` result. After a disconnect, `is_connected` can remain stale and the client is not properly reset or reconnected.
- Recommendation: detect read failures, clear connection state, and implement bounded reconnect backoff.

## 10_function_silo

### lib/10_function_silo/function_silo.h

#### run_function_silo
- Mid: `json_packet["data"]` and `json_packet["data"]["value"]` are not validated before conversion. Missing or malformed values can silently set the LED incorrectly.
- Recommendation: validate object shape and type before acting, and return a distinct error for invalid commands.
- High: `ESP.restart()` is called immediately, before the HTTP or WebSocket caller can reliably receive and flush the acknowledgment.
- Recommendation: acknowledge the command first and schedule the restart after the response is sent.
- Low: unknown commands return an empty string. HTTP returns a 400, but WebSocket sends an empty frame.
- Recommendation: return a uniform structured error response across both transports.

## 99_rgb

### lib/99_rgb/rgb.h

#### rgb::toggle
- Low: the code derives the next LED state from the pixel color but updates `isOn` independently. Those two values can drift if hardware state changes externally.
- Recommendation: use a single source of truth and route both `set()` and `toggle()` through the same state update path.

## Supporting Config

### data/config.json

#### configuration file
- High: the Wi-Fi password is stored in plaintext on the filesystem and is loaded at startup.
- Recommendation: move credentials to protected device storage such as NVS, exclude populated config files from version control, and rotate any already-exposed credential.

## Summary

The remaining high-priority issues are command authorization, unbounded request parsing, and plain-text credential storage. The remaining items should be addressed in order of impact and the recommended follow-up actions.

