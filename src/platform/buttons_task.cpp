#include "buttons_task.h"
#include "buttons.h"
#include "platform/task_registry.h"

static void buttons_task(void* param) {
    (void)param;
    for (;;) {
        Button btn = buttons_get_event();
        if (btn != BTN_NONE) {
            // TODO: send to menu or interface
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void buttons_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(buttons_task, "buttons", 2048, nullptr, 2, &handle);
    task_register(handle, "buttons", 2, 2048);
}
