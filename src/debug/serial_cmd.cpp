#include "serial_cmd.h"
#include "debug.h"
#include "task_registry.h"

static char s_cmd_buf[64];
static uint8_t s_cmd_pos = 0;

static void process_command(const char* cmd) {
    if (strlen(cmd) == 1) {
        switch (cmd[0]) {
            case 'd': debug_all();           return;
            case 't': debug_task_list();     return;
            case 'r': debug_runtime_stats(); return;
            case 'm': debug_memory();        return;
            case '?':
                Serial.println("Commands:");
                Serial.println("  d         - full debug dump");
                Serial.println("  t         - task list");
                Serial.println("  r         - runtime info");
                Serial.println("  m         - memory info");
                Serial.println("  s <name>  - suspend task");
                Serial.println("  g <name>  - resume (go) task");
                Serial.println("  ?         - this help");
                return;
        }
    }

    if (cmd[0] == 's' && cmd[1] == ' ') {
        const char* name = cmd + 2;
        if (task_registry_suspend(name)) {
            Serial.printf("Suspended: %s\n", name);
        } else {
            Serial.printf("Not found: %s\n", name);
        }
        return;
    }

    if (cmd[0] == 'g' && cmd[1] == ' ') {
        const char* name = cmd + 2;
        if (task_registry_resume(name)) {
            Serial.printf("Resumed: %s\n", name);
        } else {
            Serial.printf("Not found: %s\n", name);
        }
        return;
    }

    Serial.printf("Unknown: %s (send ? for help)\n", cmd);
}

static void serial_cmd_task(void* param) {
    (void)param;
    for (;;) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (s_cmd_pos > 0) {
                    s_cmd_buf[s_cmd_pos] = '\0';
                    process_command(s_cmd_buf);
                    s_cmd_pos = 0;
                }
            } else if (s_cmd_pos < sizeof(s_cmd_buf) - 1) {
                s_cmd_buf[s_cmd_pos++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void serial_cmd_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, nullptr, 1, &handle);
    task_register(handle, "serial_cmd", 1, 4096);
}
