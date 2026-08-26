#include "serial_cmd.h"
#include "debug.h"
#include "task_registry.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "hardware/adc.h"
#include "hardware/input_config.h"
#include "hardware/voltage_select.h"

static char s_cmd_buf[64];
static uint8_t s_cmd_pos = 0;

static void process_command(const char* cmd) {
    if (strlen(cmd) == 1) {
        switch (cmd[0]) {
            case 'd': debug_all();           return;
            case 't': debug_task_list();     return;
            case 'r': debug_runtime_stats(); return;
            case 'm': debug_memory();        return;
            case 'p':
                {
                    Serial.print("\r\n=== CloudGauge Probes ===\r\n");
                    Serial.print("Input     mA       cm\r\n");
                    Serial.print("-----   ------   ------\r\n");
                    for (uint8_t i = 0; i < 8; i++) {
                        const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
                        String json = cloudgauge_get(names[i]);
                        JsonDocument doc;
                        deserializeJson(doc, json);
                        float ma = doc["ma"];
                        float cm = doc["cm"];
                        Serial.printf("  %s     %5.2f    %5.1f\r\n", names[i], ma, cm);
                    }
                    Serial.print("\r\n");
                }
                return;
            case '?':
                Serial.println("Commands:");
                Serial.println("  d         - full debug dump");
                Serial.println("  t         - task list");
                Serial.println("  r         - runtime info");
                Serial.println("  m         - memory info");
                Serial.println("  p         - probe readings (cloudgauge)");
                Serial.println("  s <name>  - suspend task");
                Serial.println("  g <name>  - resume (go) task");
                Serial.println("  ?         - this help");
                return;
        }
    }

    if (cmd[0] == 'p' && cmd[1] == 'j') {
        Serial.println(cloudgauge_get_all());
        return;
    }

    if (cmd[0] == 'p' && cmd[1] == 'v') {
        // Measure supply voltage on both channels
        voltage_select_set_a(VOLTAGE_24V);
        voltage_select_set_b(VOLTAGE_24V);
        vTaskDelay(pdMS_TO_TICKS(100));

        Serial.print("\r\n=== Supply Voltage Check ===\r\n");
        const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
        for (uint8_t i = 0; i < 8; i++) {
            input_config_set((Input)i, SW_ANALOG, true);
            input_config_set((Input)i, SW_PULLUP, true);
            input_config_set((Input)i, SW_SHUNT, true);
            vTaskDelay(pdMS_TO_TICKS(50));
            int32_t mv = adc_read_mv((AdcInput)i);
            int32_t supply_mv = mv * 26;  // (5k+200) / 200 ≈ 26
            input_config_set((Input)i, SW_ANALOG, false);
            input_config_set((Input)i, SW_PULLUP, false);
            input_config_set((Input)i, SW_SHUNT, false);
            Serial.printf("  %s: %5ld mV ADC → %5.1f V supply\r\n", names[i], mv, supply_mv / 1000.0f);
        }
        Serial.print("\r\n");
        return;
    }

    if (cmd[0] == 'p' && cmd[1] == 'd') {
        // Stop cloudgauge task to avoid I2C collision
        cloudgauge_stop();

        // Force 24V on both channels
        voltage_select_set_a(VOLTAGE_24V);
        voltage_select_set_b(VOLTAGE_24V);
        vTaskDelay(pdMS_TO_TICKS(100));

        Serial.print("\r\n=== Probe Debug (reverse order) ===\r\n");
        const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
        // Read in reverse: B4, B3, B2, B1, A4, A3, A2, A1
        uint8_t order[] = {7, 6, 5, 4, 3, 2, 1, 0};
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t i = order[j];
            input_config_set((Input)i, SW_ANALOG, true);
            input_config_set((Input)i, SW_SHUNT, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            adc_read_mv((AdcInput)i);  // discard (MUX settle)
            vTaskDelay(pdMS_TO_TICKS(10));
            int32_t mv = adc_read_mv((AdcInput)i);  // keep
            input_config_set((Input)i, SW_ANALOG, false);
            input_config_set((Input)i, SW_SHUNT, false);
            float ma = mv / 200.0f;
            Serial.printf("  %s: %5ld mV = %5.2f mA\r\n", names[i], mv, ma);
        }
        Serial.print("\r\n");

        // Resume cloudgauge
        cloudgauge_start();
        return;
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
