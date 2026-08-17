#include "network_task.h"
#include "file_system.h"
#include "config.h"
#include "w5500.h"
#include "my_wifi.h"
#include "web_socket.h"
#include "web_server.h"
#include "rgb.h"
#include "debug/task_registry.h"

static void network_task(void* param) {
    (void)param;

    // Initialize network stack (runs once)
    file_system::init();
    config::init();
    w5500::init();
    wifi::init();
    web_socket::init();
    web_server::init();
    rgb::init();

    Serial.println("[network] Network stack initialized");

    // Poll loop
    for (;;) {
        web_server::poll();
        web_socket::poll();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void network_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(
        network_task,
        "network",
        8192,
        nullptr,
        1,
        &handle
    );
    task_register(handle, "network", 1, 8192);
}
