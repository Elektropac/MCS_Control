#include "display_task.h"
#include "hardfunc/menu.h"
#include "debug/task_registry.h"

static void display_task(void* param) {
    (void)param;

    for (;;) {
        if (menu_is_active()) {
            menu_render();
        }

        // Hurtig under animation, langsom når stationær
        if (menu_is_animating()) {
            vTaskDelay(pdMS_TO_TICKS(16));   // ~60fps under scroll
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));  // 5fps når stationær
        }
    }
}

void display_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(display_task, "display", 2048, nullptr, 1, &handle);
    task_register(handle, "display", 1, 2048);
}
