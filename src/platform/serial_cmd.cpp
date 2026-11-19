#include "serial_cmd.h"
#include "debug.h"
#include "cpu_load.h"
#include "memmark.h"
#include "ssd1306.h"
#include "task_registry.h"
#include "apps/probes/probes.h"
#include "hardware/adc.h"
#include "hardware/input_config.h"
#include "hardware/voltage_select.h"
#include "LittleFS.h"
#include <ArduinoJson.h>
#include "function_silo.h"
#include "apps/fueling_auth/txn_log.h"

// 1 KB: room for "api {...}" requests with an encrypted auth blob (~400-600 chars).
static char s_cmd_buf[1024];
static uint16_t s_cmd_pos = 0;

// Replace /config.json safely: write a temp file, check the byte count, read it back and
// parse it, and only then swap it in (LittleFS rename replaces the target in one step).
// On any failure the original config is left untouched. A truncated config would make
// config::load() fail on the next boot and lose connection + pump/probe settings.
static bool save_config_safely(JsonDocument& doc, const char* tag) {
    static const char* TMP = "/config.json.tmp";
    const size_t want = measureJsonPretty(doc);
    File o = LittleFS.open(TMP, "w");
    if (!o) { Serial.printf("[%s] cannot write %s (nothing changed)\n", tag, TMP); return false; }
    const size_t wrote = serializeJsonPretty(doc, o);
    o.close();
    bool ok = (wrote == want);
    if (ok) {
        File r = LittleFS.open(TMP, "r");
        JsonDocument check;
        ok = r && r.size() == want && !deserializeJson(check, r);
        if (r) r.close();
    }
    if (!ok) {
        LittleFS.remove(TMP);
        Serial.printf("[%s] writing the new config failed (%u of %u bytes, flash full?) - original kept, not rebooting\n",
                      tag, (unsigned)wrote, (unsigned)want);
        return false;
    }
    if (LittleFS.rename(TMP, "/config.json")) return true;       // replaces the target in one step
    // Fallback if the VFS refuses to rename onto an existing file: the new file is verified,
    // so remove the old one and try again.
    LittleFS.remove("/config.json");
    if (LittleFS.rename(TMP, "/config.json")) return true;
    Serial.printf("[%s] rename failed - %s holds the new (verified) config\n", tag, TMP);
    return false;
}

static void process_command(const char* cmd) {
    if (strlen(cmd) == 1) {
        switch (cmd[0]) {
            case 'd': debug_all();           return;
            case 'c': Serial.print("\033[2J\033[H"); return;
            case 't': debug_task_list();     return;
            case 'r': debug_runtime_stats(); return;
            case 'm': debug_memory();        return;
            case 'u': debug_cpu();           return;
            case 'k': debug_stacks();        return;
            case 'h': memmark_print();       return;
            case 'x': { cpu_load_print_tasks(); uint32_t us, n; oled_send_stats(us, n); Serial.printf("OLED frame transfer: %lu transfers, avg %lu us\n", (unsigned long)n, (unsigned long)us); } return;
            case 'p':
                {
                    Serial.print("\r\n=== CloudGauge Probes ===\r\n");
                    Serial.print("Input     mA       mm\r\n");
                    Serial.print("-----   ------   ------\r\n");
                    for (uint8_t i = 0; i < 8; i++) {
                        const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
                        String json = probes_get(names[i]);
                        JsonDocument doc;
                        deserializeJson(doc, json);
                        float ma = doc["ma"];
                        int mm = doc["mm"];
                        Serial.printf("  %s     %5.2f    %5d\r\n", names[i], ma, mm);
                    }
                    Serial.print("\r\n");
                }
                return;
            case '?':
                Serial.println("Commands:");
                Serial.println("  c         - clear screen");
                Serial.println("  d         - full debug dump");
                Serial.println("  t         - task list");
                Serial.println("  r         - runtime info");
                Serial.println("  m         - memory info");
                Serial.println("  u         - CPU load per core since last u");
                Serial.println("  k         - stack headroom per task");
                Serial.println("  h         - heap checkpoints through boot");
                Serial.println("  x         - CPU per task since last x");
                Serial.println("  p         - probe readings (cloudgauge)");
                Serial.println("  s <name>  - suspend task");
                Serial.println("  g <name>  - resume (go) task");
                Serial.println("  pr        - power reset (5 min off, then restart)");
                Serial.println("  wifi <ssid> <password> | wifi off - WiFi in config + reboot");
                Serial.println("  api <json> - run a web/BLE API request, e.g. api {\"subject\":\"setup_get\"}");
                Serial.println("  txn | txn test - transaction store status / self-test");
                Serial.println("  ?         - this help");
                return;
        }
    }

    if (cmd[0] == 'p' && cmd[1] == 'j') {
        Serial.println(probes_get_all());
        return;
    }

    // cfg {...} — save JSON config to LittleFS and reboot
    if (strncmp(cmd, "cfg ", 4) == 0) {
        const char* json = cmd + 4;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (err) {
            Serial.printf("[cfg] JSON parse error: %s\n", err.c_str());
            return;
        }
        if (!save_config_safely(doc, "cfg")) return;
        Serial.println("[cfg] Config saved to LittleFS. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
        return;
    }

    // wifi <ssid> <password> — enable WiFi in /config.json (keeps the rest of
    // the file) and reboot. For provisioning a unit that has no Ethernet cable;
    // the 256-byte serial line is far too short for "cfg {...}" with a full config.
    if (strncmp(cmd, "wifi ", 5) == 0) {
        const char* arg = cmd + 5;
        const bool off = (strcmp(arg, "off") == 0);
        const char* sp = strchr(arg, ' ');
        if (!off && (!sp || sp == arg)) { Serial.println("usage: wifi <ssid> <password> | wifi off"); return; }
        String ssid = off ? String() : String(arg).substring(0, (unsigned int)(sp - arg));
        String pass = off ? String() : String(sp + 1);

        File f = LittleFS.open("/config.json", "r");
        if (!f) { Serial.println("[wifi] cannot open /config.json"); return; }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) { Serial.printf("[wifi] config parse error: %s (nothing changed)\n", err.c_str()); return; }

        JsonObject w = doc["config"]["connection"]["settings"]["wifi"].to<JsonObject>();
        w["enabled"]  = !off;
        w["ssid"]     = off ? "MCS_Dev" : ssid.c_str();   // "off" restores the shipped default
        w["password"] = off ? "" : pass.c_str();

        // Temp file + verify + rename: neither a power cut nor a full flash can leave a broken config.
        if (!save_config_safely(doc, "wifi")) return;
        if (off) Serial.println("[wifi] disabled. Rebooting...");
        else     Serial.printf("[wifi] saved: ssid '%s'. Rebooting...\n", ssid.c_str());
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
        return;
    }

    // api <json> — run a function_silo request (same backend as web/BLE) and
    // print the reply. E.g.  api {"subject":"config_get"}
    if (strncmp(cmd, "api ", 4) == 0) {
        JsonDocument req;
        DeserializationError err = deserializeJson(req, cmd + 4);
        if (err) { Serial.printf("[api] JSON parse error: %s\n", err.c_str()); return; }
        String out = function_silo::run_function_silo(req);
        Serial.printf("[api] %u bytes\n", (unsigned)out.length());
        Serial.println(out);
        return;
    }

    // txn / txn test — transaction store status / self-test (separate file)
    if (strcmp(cmd, "txn") == 0) {
        Serial.printf("txn_log: %u pending, next id %lu, file %u bytes\n",
                      (unsigned)txn_log_pending_count(), (unsigned long)txn_log_next_id(),
                      (unsigned)txn_log_file_size());
        return;
    }
    if (strcmp(cmd, "txn test") == 0) { txn_log_selftest(); return; }

    // cfg? — print current config from LittleFS
    if (strcmp(cmd, "cfg?") == 0) {
        File f = LittleFS.open("/config.json", "r");
        if (!f) {
            Serial.println("[cfg] No config file found");
            return;
        }
        while (f.available()) {
            Serial.write(f.read());
        }
        f.close();
        Serial.println();
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
        probes_stop();

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
        probes_start();
        return;
    }

    if (cmd[0] == 'p' && cmd[1] == 'r') {
        Serial.print("\r\n=== PROBE POWER RESET ===\r\n");
        probes_stop();
        Serial.println("CloudGauge stopped");

        // All inputs off
        for (uint8_t i = 0; i < 8; i++) {
            input_config_set((Input)i, SW_ANALOG, false);
            input_config_set((Input)i, SW_PULLUP, false);
            input_config_set((Input)i, SW_SHUNT, false);
        }

        voltage_select_set_a(VOLTAGE_OFF);
        voltage_select_set_b(VOLTAGE_OFF);
        Serial.println("Both channels OFF — probes unpowered");
        Serial.println("Waiting 5 minutes...");

        for (int i = 300; i > 0; i--) {
            if (i % 60 == 0) {
                Serial.printf("  %d min remaining...\r\n", i / 60);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        voltage_select_set_a(VOLTAGE_24V);
        voltage_select_set_b(VOLTAGE_24V);
        Serial.println("Power restored (24V)");
        Serial.println("Waiting 10s for probe startup...");
        vTaskDelay(pdMS_TO_TICKS(10000));

        probes_start();
        Serial.println("CloudGauge restarted");
        Serial.println("=== DONE — use 'p' to check readings ===\r\n");
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
    // Stack: 2048 was too small — ANY serial command (even 'm') overflowed it and
    // hit the stack canary ("Stack canary watchpoint triggered (serial_cmd)"),
    // because process_command() has big frames (JsonDocument/String locals,
    // printf with floats) and cfg?/pd etc. go deeper into LittleFS. 6 KB for now;
    // trim after measuring headroom with the 'k' command.
    // 6 KB: the 'api' command runs function_silo requests here (same code the
    // web server runs on its 6 KB stack); setup_get overflowed 4 KB. Simple
    // commands peak at ~2.6 KB, config_get left 0.7 KB of 4 KB.
    const uint32_t stack = 6144;
    xTaskCreate(serial_cmd_task, "serial_cmd", stack, nullptr, 1, &handle);
    task_register(handle, "serial_cmd", 1, stack);
}
