#include "serial_cmd.h"
#include "debug.h"
#include "task_registry.h"
#include "hardfunc/menu.h"
#include "hardfunc/voltage_select.h"
#include "hardfunc/version.h"
#include "hardfunc/relays.h"
#include "hardfunc/adc.h"
#include "hardfunc/input_config.h"
#include "buttons.h"
#include "hal.h"
#include "pins.h"
#include "tca9535.h"

static char s_cmd_buf[64];
static uint8_t s_cmd_pos = 0;
static uint8_t s_port0_test = 0xFF;  // for raw port0 test writes

static void i2c_scan() {
    // Known devices on the Control board
    struct { uint8_t addr; const char* name; } known[] = {
        { ADDR_VOLTAGE_SELECT, "TCA9535 Voltage/Version (0x21)" },
        { ADDR_INPUT_CONFIG_B, "TCA9535 Input Config B (0x23)" },
        { ADDR_INPUT_CONFIG_A, "TCA9535 Input Config A (0x25)" },
        { ADDR_SERIAL_CONTROL, "TCA9535 Serial/Relay  (0x27)" },
        { ADDR_ADC_A,          "ADS1115 ADC-A         (0x48)" },
        { ADDR_ADC_B,          "ADS1115 ADC-B         (0x49)" },
    };

    Serial.println("\n=== I2C Scan (SDA=33, SCL=34, 400kHz) ===");
    Serial.println("Addr  Status   Device");
    Serial.println("----  ------   ------");

    int found = 0;
    for (auto& dev : known) {
        bool online = i2c_probe(dev.addr);
        Serial.printf("0x%02X  %s   %s\n",
            dev.addr,
            online ? "  OK  " : "  --  ",
            dev.name);
        if (online) found++;
    }

    // Also scan for unknown devices
    Serial.println("\nFull scan (0x08-0x77):");
    int unknown = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        // Skip known addresses (already printed above)
        bool is_known = false;
        for (auto& dev : known) {
            if (dev.addr == addr) { is_known = true; break; }
        }
        if (is_known) continue;

        if (i2c_probe(addr)) {
            Serial.printf("  0x%02X — UNKNOWN device!\n", addr);
            unknown++;
            found++;
        }
    }
    if (unknown == 0) Serial.println("  (no unknown devices)");

    Serial.printf("\nTotal: %d device(s) found\n\n", found);
}

static void voltage_cmd(const char* cmd) {
    // cmd is "va X" or "vb X" where X = 0/5/12/24
    char ch = cmd[1];
    int val = atoi(cmd + 3);
    Voltage v;
    switch (val) {
        case 0:  v = VOLTAGE_OFF; break;
        case 5:  v = VOLTAGE_5V;  break;
        case 12: v = VOLTAGE_12V; break;
        case 24: v = VOLTAGE_24V; break;
        default:
            Serial.printf("Invalid voltage: %d (use 0/5/12/24)\n", val);
            return;
    }
    if (ch == 'a') {
        voltage_select_set_a(v);
        Serial.printf("Channel A → %dV\n", val);
    } else if (ch == 'b') {
        voltage_select_set_b(v);
        Serial.printf("Channel B → %dV\n", val);
    } else {
        Serial.println("Use: va <0/5/12/24> or vb <0/5/12/24>");
    }
}

static void adc_read_all() {
    const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
    Serial.println("\n=== ADC Readings ===");
    for (int i = 0; i < 8; i++) {
        int32_t mv = adc_read_mv((AdcInput)i);
        Serial.printf("  %s: %6ld mV\n", names[i], mv);
    }
    Serial.println();
}

static void inputs_voltage_mode() {
    for (int i = 0; i < 8; i++) {
        input_config_mode((Input)i, MODE_VOLTAGE);
    }
    Serial.println("All inputs → VOLTAGE mode");
}

static void inputs_ma_mode() {
    for (int i = 0; i < 8; i++) {
        input_config_mode((Input)i, MODE_MA);
    }
    Serial.println("All inputs → mA mode (shunt)");
}

static void inputs_off() {
    for (int i = 0; i < 8; i++) {
        input_config_mode((Input)i, MODE_OFF);
    }
    Serial.println("All inputs → OFF");
}

static void inputs_selftest() {
    const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
    const uint8_t dig_pins[] = {A1_DIGITAL, A2_DIGITAL, A3_DIGITAL, A4_DIGITAL,
                                B1_DIGITAL, B2_DIGITAL, B3_DIGITAL, B4_DIGITAL};

    // Configure digital pins as inputs
    for (int i = 0; i < 8; i++) {
        pinMode(dig_pins[i], INPUT);
    }

    Serial.println("\n=== Self-test ===");

    // --- Analog test at 5V only ---
    // At 12V/24V the pullup would push the ADC input above 5.2V clamp!
    // Signal path: Pullup(5k)→input point→10k→ADC→10k→GND
    // At 5V: input ~4.5V, ADC sees ~2.2V (safe)
    // At 12V: input ~12V, ADC sees ~6V (CLAMP!)
    voltage_select_set_a(VOLTAGE_5V);
    voltage_select_set_b(VOLTAGE_5V);
    delay(10);

    for (int i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_ANALOG, true);
        input_config_set((Input)i, SW_PULLUP, true);
        input_config_set((Input)i, SW_SHUNT, false);
        input_config_set((Input)i, SW_DIGITAL, false);
    }
    delay(50);

    Serial.println("-- Analog 5V: pullup (expect ~4.5V) --");
    for (int i = 0; i < 8; i++) {
        int32_t mv = adc_read_mv((AdcInput)i);
        // Back-calculate supply: measured = supply * 20k/(5k+20k) = supply * 0.8
        int32_t supply = mv * 100 / 80;
        const char* status = (mv > 3000 && mv < 6000) ? "OK" : "FAIL";
        Serial.printf("  %s: %5ld mV [%s]  (supply ~%ld mV)\n", names[i], mv, status, supply);
    }

    // Shunt test: pullup + shunt → voltage divider (~230 mV with 5k/200R)
    for (int i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_SHUNT, true);
    }
    delay(50);

    Serial.println("-- Analog 5V: pullup + shunt (expect ~200 mV) --");
    for (int i = 0; i < 8; i++) {
        int32_t mv = adc_read_mv((AdcInput)i);
        const char* status = (mv > 50 && mv < 500) ? "OK" : "FAIL";
        Serial.printf("  %s: %5ld mV [%s]\n", names[i], mv, status);
    }

    // --- Supply voltage measurement ---
    // With pullup(5k) + shunt(200R) + analog: safe at all voltages
    // Formula: Supply = measured × (5000+200)/200 = measured × 26
    // One input at a time to minimize load on DC-DC (~5mA vs ~40mA for all 8)
    Voltage supply_levels[] = {VOLTAGE_5V, VOLTAGE_12V, VOLTAGE_24V};
    const char* supply_names[] = {"5V", "12V", "24V"};
    int32_t expected_min[] = {4000, 10000, 20000};
    int32_t expected_max[] = {8000, 18000, 35000};

    for (int v = 0; v < 3; v++) {
        voltage_select_set_a(supply_levels[v]);
        voltage_select_set_b(supply_levels[v]);
        delay(50);

        Serial.printf("-- Supply %s (pullup+shunt+analog) --\n", supply_names[v]);
        for (int i = 0; i < 8; i++) {
            // Enable only this one input
            input_config_set((Input)i, SW_ANALOG, true);
            input_config_set((Input)i, SW_PULLUP, true);
            input_config_set((Input)i, SW_SHUNT, true);
            input_config_set((Input)i, SW_DIGITAL, false);
            delay(20);

            int32_t mv = adc_read_mv((AdcInput)i);
            int32_t supply_mv = mv * 26;
            const char* status = (supply_mv > expected_min[v] && supply_mv < expected_max[v]) ? "OK" : "FAIL";
            Serial.printf("  %s: %5ld mV [%s]\n", names[i], supply_mv, status);

            // Disable again
            input_config_set((Input)i, SW_ANALOG, false);
            input_config_set((Input)i, SW_PULLUP, false);
            input_config_set((Input)i, SW_SHUNT, false);
        }
    }
    Serial.println();
    Voltage test_volts[] = {VOLTAGE_5V, VOLTAGE_12V, VOLTAGE_24V};
    const char* volt_names[] = {"5V", "12V", "24V"};

    for (int v = 0; v < 3; v++) {
        voltage_select_set_a(test_volts[v]);
        voltage_select_set_b(test_volts[v]);
        delay(10);

        // Digital HIGH test: pullup ON → opto conducts → GPIO reads LOW
        for (int i = 0; i < 8; i++) {
            input_config_set((Input)i, SW_ANALOG, false);
            input_config_set((Input)i, SW_PULLUP, true);
            input_config_set((Input)i, SW_SHUNT, false);
            input_config_set((Input)i, SW_DIGITAL, true);
        }
        delay(50);

        Serial.printf("-- Digital %s HIGH: pullup (expect LOW — opto) --\n", volt_names[v]);
        for (int i = 0; i < 8; i++) {
            int dig = digitalRead(dig_pins[i]);
            Serial.printf("  %s: DIG=%d [%s]\n", names[i], dig, dig == LOW ? "OK" : "FAIL");
        }

        // Digital LOW test: shunt ON, pullup OFF → no current → GPIO reads HIGH
        for (int i = 0; i < 8; i++) {
            input_config_set((Input)i, SW_PULLUP, false);
            input_config_set((Input)i, SW_SHUNT, true);
        }
        delay(50);

        Serial.printf("-- Digital %s LOW: shunt (expect HIGH — opto) --\n", volt_names[v]);
        for (int i = 0; i < 8; i++) {
            int dig = digitalRead(dig_pins[i]);
            Serial.printf("  %s: DIG=%d [%s]\n", names[i], dig, dig == HIGH ? "OK" : "FAIL");
        }
    }
    Serial.println();

    // Cleanup
    for (int i = 0; i < 8; i++) {
        input_config_mode((Input)i, MODE_OFF);
    }
    voltage_select_set_a(VOLTAGE_OFF);
    voltage_select_set_b(VOLTAGE_OFF);
}

static void version_cmd() {
    uint8_t hw = version_hardware();
    uint8_t mod = version_module();
    Serial.printf("Hardware version: %d\n", hw);
    Serial.printf("Module version:   %d\n", mod);
}

static void port0_write(uint8_t val) {
    if (!i2c_take(100)) { Serial.println("I2C busy"); return; }
    TCA9535 exp(ADDR_VOLTAGE_SELECT);
    exp.write_port(0, val);
    i2c_give();
    Serial.printf("Port0 = 0x%02X = ", val);
    for (int i = 7; i >= 0; i--) Serial.print((val >> i) & 1);
    Serial.println();
}

static void process_command(const char* cmd) {
    // Make a lowercase copy for easier matching
    char lc[64];
    size_t len = strlen(cmd);
    if (len >= sizeof(lc)) len = sizeof(lc) - 1;
    for (size_t i = 0; i < len; i++) lc[i] = tolower(cmd[i]);
    lc[len] = '\0';

    // Single character commands
    if (len == 1) {
        switch (lc[0]) {
            case 'd': debug_all();           return;
            case 't': debug_task_list();     return;
            case 'r': debug_runtime_stats(); return;
            case 'm': debug_memory();        return;
            case 'i': i2c_scan();            return;
            case 'v': version_cmd();         return;
            case '?':
                Serial.println("\nCommands:");
                Serial.println("  d           full debug dump");
                Serial.println("  t           task list");
                Serial.println("  r           runtime info");
                Serial.println("  m           memory info");
                Serial.println("  i           I2C bus scan");
                Serial.println("  v           HW/module version");
                Serial.println("  va <0/5/12/24>  channel A voltage");
                Serial.println("  vb <0/5/12/24>  channel B voltage");
                Serial.println("  s <name>    suspend task");
                Serial.println("  g <name>    resume task");
                Serial.println("  8/2/4/6/5   menu nav");
                Serial.println("  ?           this help\n");
                return;
        }
    }

    // Voltage commands: va0, va5, va12, va24, vb0, vb5, vb12, vb24
    if (len >= 3 && lc[0] == 'v' && (lc[1] == 'a' || lc[1] == 'b')) {
        char ch = lc[1];
        int val = atoi(lc + 2);
        Voltage volt;
        switch (val) {
            case 0:  volt = VOLTAGE_OFF; break;
            case 5:  volt = VOLTAGE_5V;  break;
            case 12: volt = VOLTAGE_12V; break;
            case 24: volt = VOLTAGE_24V; break;
            default:
                Serial.printf("Invalid: %d (use va0/va5/va12/va24)\n", val);
                return;
        }
        if (ch == 'a') {
            voltage_select_set_a(volt);
            Serial.printf("Channel A → %dV\n", val);
        } else {
            voltage_select_set_b(volt);
            Serial.printf("Channel B → %dV\n", val);
        }
        return;
    }

    // "va" or "vb" without number
    if (len == 2 && lc[0] == 'v' && (lc[1] == 'a' || lc[1] == 'b')) {
        Serial.println("Use: va0 va5 va12 va24 (same for vb)");
        return;
    }

    if (lc[0] == 's' && lc[1] == ' ') {
        const char* name = cmd + 2;
        if (task_registry_suspend(name)) {
            Serial.printf("Suspended: %s\n", name);
        } else {
            Serial.printf("Not found: %s\n", name);
        }
        return;
    }

    if (lc[0] == 'g' && lc[1] == ' ') {
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

// --- Voltage state machine ---
// Collects 'v' + 'a'/'b' + digits, executes when complete
static enum { VS_IDLE, VS_GOT_V, VS_GOT_CH } s_vstate = VS_IDLE;
static char s_vchannel = 0;
static char s_vdigits[4];
static uint8_t s_vdigit_pos = 0;

static void vs_reset() {
    s_vstate = VS_IDLE;
    s_vchannel = 0;
    s_vdigit_pos = 0;
}

static void vs_execute() {
    s_vdigits[s_vdigit_pos] = '\0';
    int val = atoi(s_vdigits);
    Voltage volt;
    switch (val) {
        case 0:  volt = VOLTAGE_OFF; break;
        case 5:  volt = VOLTAGE_5V;  break;
        case 12: volt = VOLTAGE_12V; break;
        case 24: volt = VOLTAGE_24V; break;
        default:
            Serial.printf("Invalid: %d (use 0/5/12/24)\n", val);
            vs_reset();
            return;
    }
    if (s_vchannel == 'a') {
        voltage_select_set_a(volt);
        Serial.printf("A→%dV\n", val);
    } else {
        voltage_select_set_b(volt);
        Serial.printf("B→%dV\n", val);
    }
    vs_reset();
}

// Returns true if character was consumed by voltage state machine
static bool vs_feed(char c) {
    c = tolower(c);
    switch (s_vstate) {
        case VS_IDLE:
            if (c == 'v') { s_vstate = VS_GOT_V; return true; }
            return false;
        case VS_GOT_V:
            if (c == 'a' || c == 'b') {
                s_vchannel = c;
                s_vstate = VS_GOT_CH;
                s_vdigit_pos = 0;
                return true;
            }
            // Not a/b — it was 'v' for version, put back
            vs_reset();
            return false;
        case VS_GOT_CH:
            if (c >= '0' && c <= '9' && s_vdigit_pos < 2) {
                s_vdigits[s_vdigit_pos++] = c;
                // Execute immediately if we have enough (5=1 digit, 12/24=2 digits)
                if (s_vdigit_pos == 2 || c == '0' || c == '5') {
                    vs_execute();
                }
                return true;
            }
            // Got non-digit — execute what we have if anything
            if (s_vdigit_pos > 0) vs_execute();
            else vs_reset();
            return false;
    }
    return false;
}

static void serial_cmd_task(void* param) {
    (void)param;
    for (;;) {
        while (Serial.available()) {
            char c = Serial.read();

            // Skip CR/LF
            if (c == '\n' || c == '\r') {
                // If voltage state machine is waiting, handle it
                if (s_vstate == VS_GOT_V) {
                    // 'v' + Enter = version command
                    version_cmd();
                    vs_reset();
                } else if (s_vstate == VS_GOT_CH && s_vdigit_pos > 0) {
                    vs_execute();
                } else {
                    vs_reset();
                }
                // Process buffered command if any
                if (s_cmd_pos > 0) {
                    s_cmd_buf[s_cmd_pos] = '\0';
                    process_command(s_cmd_buf);
                    s_cmd_pos = 0;
                }
                continue;
            }

            // Try voltage state machine first
            if (vs_feed(c)) continue;

            // Menu keys respond immediately
            switch (c) {
                case '8': menu_handle_button(BTN_UP);    continue;
                case '2': menu_handle_button(BTN_DOWN);  continue;
                case '4': menu_handle_button(BTN_LEFT);  continue;
                case '6': menu_handle_button(BTN_RIGHT); continue;
                case '5': menu_handle_button(BTN_OK);    continue;
            }

            // Single-char instant commands
            char lc = tolower(c);
            switch (lc) {
                case 'a': adc_read_all();        continue;
                case 'b': {
                    // Show raw button ADC for 10 seconds
                    Serial.println("Button ADC (10s) — press buttons:");
                    unsigned long end = millis() + 10000;
                    Button last = BTN_NONE;
                    while (millis() < end) {
                        int val = analogRead(PIN_BUTTON);
                        Button raw = buttons_read_raw();
                        if (raw != last) {
                            Serial.printf("  ADC=%4d  → %s\n", val, buttons_to_text(raw));
                            last = raw;
                        }
                        delay(50);
                    }
                    Serial.println("Done.\n");
                    continue;
                }
                case 'd': debug_all();           continue;
                case 'f': inputs_voltage_mode(); continue;
                case 'n': inputs_ma_mode();      continue;
                case 'o': inputs_off();          continue;
                case 'w': inputs_selftest();    continue;
                case 't': debug_task_list();     continue;
                case 'r': debug_runtime_stats(); continue;
                case 'm': debug_memory();        continue;
                case 'i': i2c_scan();            continue;
                case 'x':  // toggle relay A
                    relay_set(RELAY_A, !relay_get(RELAY_A));
                    Serial.printf("Relay A → %s\n", relay_get(RELAY_A) ? "ON" : "OFF");
                    continue;
                case 'y':  // toggle relay B
                    relay_set(RELAY_B, !relay_get(RELAY_B));
                    Serial.printf("Relay B → %s\n", relay_get(RELAY_B) ? "ON" : "OFF");
                    continue;
                case 'h':  // SSR test: all B inputs HIGH (pullup on, shunt off)
                    voltage_select_set_b(VOLTAGE_12V);
                    for (int i = INPUT_B1; i <= INPUT_B4; i++) {
                        input_config_set((Input)i, SW_PULLUP, true);
                        input_config_set((Input)i, SW_SHUNT, false);
                        input_config_set((Input)i, SW_ANALOG, false);
                        input_config_set((Input)i, SW_DIGITAL, false);
                    }
                    Serial.println("B1-B4: HIGH (12V pullup x4)");
                    continue;
                case 'l':  // SSR test: all B inputs LOW (pullup off, shunt on)
                    for (int i = INPUT_B1; i <= INPUT_B4; i++) {
                        input_config_set((Input)i, SW_PULLUP, false);
                        input_config_set((Input)i, SW_SHUNT, true);
                        input_config_set((Input)i, SW_ANALOG, false);
                        input_config_set((Input)i, SW_DIGITAL, false);
                    }
                    Serial.println("B1-B4: LOW (shunt x4)");
                    continue;
                case '?':
                    Serial.println("\nCommands:");
                    Serial.println("  a    read all ADC channels");
                    Serial.println("  f    all inputs → voltage mode");
                    Serial.println("  n    all inputs → mA mode");
                    Serial.println("  o    all inputs → off");
                    Serial.println("  w    self-test (5V+pullup→ADC)");
                    Serial.println("  d    full debug dump");
                    Serial.println("  t    task list");
                    Serial.println("  r    runtime info");
                    Serial.println("  m    memory info");
                    Serial.println("  i    I2C scan");
                    Serial.println("  v    HW version");
                    Serial.println("  va5  channel A 5V");
                    Serial.println("  va12 channel A 12V");
                    Serial.println("  va24 channel A 24V");
                    Serial.println("  va0  channel A off");
                    Serial.println("  vb.. same for B");
                    Serial.println("  x    toggle relay A");
                    Serial.println("  y    toggle relay B");
                    Serial.println("  8/2/4/6/5 menu nav");
                    Serial.println("  ?    this help\n");
                    continue;
            }

            // Buffer anything else (for 's name' / 'g name')
            if (s_cmd_pos < sizeof(s_cmd_buf) - 1) {
                s_cmd_buf[s_cmd_pos++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void serial_cmd_start_task() {
    TaskHandle_t handle = nullptr;
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, nullptr, 1, &handle);
    task_register(handle, "serial_cmd", 1, 4096);
}
