#include "display_task.h"
#include "hardfunc/menu.h"
#include "ssd1306.h"
#include "debug/task_registry.h"

static void display_task(void* param) {
    (void)param;

    for (;;) {
        ScreenRenderFunc screen = menu_get_screen();
        if (screen) {
            // Custom full-screen view (e.g. tank graphic)
            oled_begin();
            screen();
            oled_end();
            vTaskDelay(pdMS_TO_TICKS(50));  // 20fps for animated views
        } else if (menu_is_active()) {
            menu_render();

            if (menu_is_animating()) {
                vTaskDelay(pdMS_TO_TICKS(16));
            } else {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void display_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(display_task, "display", 2048, nullptr, 1, &handle);
    task_register(handle, "display", 1, 2048);
}
