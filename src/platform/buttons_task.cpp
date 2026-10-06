#include "buttons_task.h"
#include "buttons.h"
#include "platform/task_registry.h"
#include "apps/product_display/product_display.h"
#include "apps/menu/menu.h"

static void buttons_task(void* param) {
    (void)param;
    for (;;) {
        Button btn = buttons_get_event();
        if (btn != BTN_NONE) {
            // Any press counts as activity. If this woke the panel, swallow it
            // (user just wanted the screen back). Otherwise navigate the menu.
            bool was_wake = product_display_mark_activity();
            if (!was_wake) {
                menu_handle_button(btn);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void buttons_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(buttons_task, "buttons", 2048, nullptr, 2, &handle);
    task_register(handle, "buttons", 2, 2048);
}
