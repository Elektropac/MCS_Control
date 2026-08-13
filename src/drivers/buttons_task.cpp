#include "buttons_task.h"
#include "buttons.h"

void buttons_check() {
    Button btn = buttons_get_event();
    if (btn != BTN_NONE) {
        // TODO: send to menu or interface
    }
}
