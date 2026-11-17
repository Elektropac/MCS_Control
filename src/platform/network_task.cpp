#include "network_task.h"

#include "w5500.h"
#include "my_wifi.h"
#include "web_socket.h"
#include "queues.h"
#include "web_server.h"
#include "bluetooth.h"

#include "platform/task_registry.h"
#include "apps/service_ap/service_ap.h"

bool network_initialized = false;

static void network_task(void* param) {
    (void)param;

    // Initialize network stack (runs once).
    // file_system and config already init'd in setup().
    // web_server / web_socket are initialized by their own tasks below.
    w5500::init();
    wifi::init();

    // The web server task only waits ~5 s for this function before it starts
    // listening, and w5500::init() can take a minute on DHCP without a cable —
    // so WiFi usually connects AFTER web_server::init() already looked at
    // wifi::connected. Start the WiFi listener here once we are really up
    // (begin() on an already-listening server is a no-op).
    if (wifi::connected) web_server::wifi_begin();

    network_initialized = true;

    // Poll loop
    for (;;) {
        w5500::poll();
        wifi::poll();
        service_ap::poll();        // captive-portal DNS (no-op when AP off)
        // w5500::poll()/wifi::poll() are empty stubs and the DNS server is off
        // (service AP disabled), so there is nothing to service at 100 Hz.
        // 50 ms keeps the captive-portal DNS responsive if the AP comes back.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void web_server_task(void* param) {
    (void)param;

    // Wait for the network stack, but DON'T block indefinitely: w5500::init()
    // can sit in DHCP for ~60s on a bench / Ethernet-less install, and the
    // local web UI (served over the service-AP WiFi) must not be held hostage
    // by that. Cap the wait at ~5s, then start listening regardless. poll()
    // re-checks wifi::connected each loop, so AP clients get served once the
    // service AP flips it true.
    uint32_t t0 = millis();
    while (!network_initialized && (millis() - t0) < 5000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    web_server::init();

    // Poll loop
    for (;;) {
        web_server::poll();
        // Every poll asks the W5500 about its sockets over SPI. 20 ms halves
        // that bus traffic/CPU; a page request waits at most 20 ms to be picked up.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void web_socket_task(void* param) {
    (void)param;
    
    while(!network_initialized) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    web_socket::init();
    queues::init();

    // Poll loop
    for (;;) {
        web_socket::poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void bluetooth_task(void* param) {
    (void)param;

    // NOTE: bluetooth::init() is now called early in setup() — before the
    // network stack fragments the heap — so BLE gets the large contiguous
    // block it needs. This task only runs the poll loop.
    for (;;) {
        bluetooth::poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


void network_start_task() {
    TaskHandle_t networkHandle = nullptr;
    TaskHandle_t webServerHandle = nullptr;
    TaskHandle_t webSocketHandle = nullptr;
    TaskHandle_t bluetoothHandle = nullptr;
    // Stack sizes right-sized from measured high-water marks (memreport):
    //   network ~2.0K used, web_server ~4.1K, web_socket ~0.8K, bluetooth ~0.8K.
    // Kept with a generous safety margin over the worst observed usage.
    int networkSize    = 1024 * 4;   // used ~2.0K
    int webServerSize  = 1024 * 6;   // used ~4.1K (HTTP parsing peaks)
    int webSocketSize  = 1024 * 6;   // TLS/mbedtls call depth under handshake
    int bluetoothSize  = 1024 * 8;   // BLE + mbedtls GCM under active session
                                     // (idle ~0.8K but spikes to ~3.3K+ with a
                                     // connected client doing encrypt/decrypt)

    xTaskCreate(
        network_task,
        "network",
        networkSize,
        nullptr,
        1,
        &networkHandle
    );
    
    task_register(networkHandle, "network", 1, networkSize);

    xTaskCreate(
        web_server_task,
        "web_server",
        webServerSize,
        nullptr,
        1,
        &webServerHandle
    );
    task_register(webServerHandle, "web_server", 1, webServerSize);

    xTaskCreate(
        web_socket_task,
        "web_socket",
        webSocketSize,
        nullptr,
        1,
        &webSocketHandle
    );
    task_register(webSocketHandle, "web_socket", 1, webSocketSize);

    xTaskCreate(
        bluetooth_task,
        "bluetooth",
        bluetoothSize,
        nullptr,
        1,
        &bluetoothHandle
    );
    task_register(bluetoothHandle, "bluetooth", 1, bluetoothSize);
}


