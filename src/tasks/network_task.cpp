#include "network_task.h"

#include "w5500.h"
#include "my_wifi.h"
#include "web_socket.h"
#include "web_server.h"
#include "debug/task_registry.h"

bool network_initialized = false;

static void network_task(void* param) {
    (void)param;

    // Initialize network stack (runs once)
    
    w5500::init();
    wifi::init();

    network_initialized = true;

    // Poll loop
    for (;;) {
        w5500::poll();
        wifi::poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void web_server_task(void* param) {
    (void)param;

    while(!network_initialized) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    web_server::init();

    // Poll loop
    for (;;) {
        web_server::poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void web_socket_task(void* param) {
    (void)param;
    
    while(!network_initialized) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    web_socket::init();

    // Poll loop
    for (;;) {
        web_socket::poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


void network_start_task() {
    TaskHandle_t networkHandle = nullptr;
    TaskHandle_t webServerHandle = nullptr;
    TaskHandle_t webSocketHandle = nullptr;
    int networkSize = 1024 * 8;
    int webServerSize = 1024 * 8;
    int webSocketSize = 1024 * 8;

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
}


