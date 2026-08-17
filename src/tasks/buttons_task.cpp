#include "buttons_task.h"
#include "buttons.h"
#include "hardfunc/menu.h"
#include "hardfunc/menu_icons.h"
#include "debug/task_registry.h"

// --- Menu items ---
static void action_placeholder() {
    // TODO: connect to real functions
}

static const MenuItem main_menu[] = {
    { "Tanks",       icon_tanks_frames,   action_placeholder, nullptr, 0 },
    { "Pumps",       icon_pumps_frames,   action_placeholder, nullptr, 0 },
    { "Config",      icon_config_frames,  action_placeholder, nullptr, 0 },
    { "Network",     icon_network_frames, action_placeholder, nullptr, 0 },
    { "Reboot",      icon_reboot_frames,  action_placeholder, nullptr, 0 },
    { "Diagnostics", icon_diag_frames,    action_placeholder, nullptr, 0 },
};

static void buttons_task(void* param) {
    (void)param;

    // Initialize menu
    menu_init(main_menu, 6);

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
