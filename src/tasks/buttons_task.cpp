#include "buttons_task.h"
#include "buttons.h"
#include "hardfunc/menu.h"
#include "hardfunc/menu_registry.h"
#include "debug/task_registry.h"

#define MAX_MENU_ITEMS 10
static MenuItem s_menu_items[MAX_MENU_ITEMS];

static void buttons_task(void* param) {
    (void)param;

    // Build menu from config (or fallback to full registry)
    menu_registry_init();
    uint8_t count = menu_build_from_config(s_menu_items, MAX_MENU_ITEMS);
    menu_init(s_menu_items, count);

    for (;;) {
        Button btn = buttons_get_event();
        if (btn != BTN_NONE) {
            menu_handle_button(btn);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void buttons_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(buttons_task, "buttons", 2048, nullptr, 2, &handle);
    task_register(handle, "buttons", 2, 2048);
}
