#include "serial_cmd.h"
#include "debug.h"

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
                Serial.println("Commands: d=all, t=tasks, r=runtime, m=memory");
                return;
        }
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
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, nullptr, 1, nullptr);
}
