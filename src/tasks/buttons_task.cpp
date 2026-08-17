#include "buttons_task.h"
#include "buttons.h"

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
    xTaskCreate(buttons_task, "buttons", 2048, nullptr, 2, nullptr);
}
