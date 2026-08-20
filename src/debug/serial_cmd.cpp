#include "serial_cmd.h"
#include "debug.h"
#include "task_registry.h"
#include "hardfunc/menu.h"
#include "hardfunc/voltage_select.h"
#include "hardfunc/version.h"
#include "hardfunc/relays.h"
#include "hardfunc/adc.h"
#include "hardfunc/input_config.h"
#include "hardfunc/serial_control.h"
#include "buzzer.h"
#include "buttons.h"
#include "hal.h"
#include "pins.h"
#include "tca9535.h"

static bool s_uart_a_init = false;
static bool s_uart_b_init = false;

static void uart_init_a(uint32_t baud = 9600) {
    Serial1.begin(baud, SERIAL_8N1, UART_A_RX, UART_A_TX);
    s_uart_a_init = true;
    Serial.printf("UART A init @ %d baud (TX=%d, RX=%d)\n", baud, UART_A_TX, UART_A_RX);
}

static void uart_init_b(uint32_t baud = 9600) {
    Serial2.begin(baud, SERIAL_8N1, UART_B_RX, UART_B_TX);
    s_uart_b_init = true;
    Serial.printf("UART B init @ %d baud (TX=%d, RX=%d)\n", baud, UART_B_TX, UART_B_RX);
}

static void uart_test_cmd(const char* cmd) {
    // Commands:
    //   ua232  — set ch A to RS-232 mode + init
    //   ua485  — set ch A to RS-485 mode + init
    //   ub232  — set ch B to RS-232 mode + init
    //   ub485  — set ch B to RS-485 mode + init
    //   uat    — toggle ch A termination
    //   ubt    — toggle ch B termination
    //   usa XX — send string XX on ch A
    //   usb XX — send string XX on ch B
    //   ul     — listen on both (5 sec, shows received bytes)
    //   uloop  — loopback test: send on A, listen on B (and vice versa)

    if (strlen(cmd) < 2) {
        Serial.println("UART commands:");
        Serial.println("  ua232   Ch A → RS-232");
        Serial.println("  ua485   Ch A → RS-485");
        Serial.println("  ub232   Ch B → RS-232");
        Serial.println("  ub485   Ch B → RS-485");
        Serial.println("  uat     Toggle Ch A termination");
        Serial.println("  ubt     Toggle Ch B termination");
        Serial.println("  usa XX  Send XX on Ch A");
        Serial.println("  usb XX  Send XX on Ch B");
        Serial.println("  ul      Listen 5s (both channels)");
        Serial.println("  uloop   Loopback A→B + B→A");
        return;
    }

    char sub[16];
    size_t slen = strlen(cmd + 1);
    if (slen >= sizeof(sub)) slen = sizeof(sub) - 1;
    for (size_t i = 0; i < slen; i++) sub[i] = tolower(cmd[1 + i]);
    sub[slen] = '\0';

    // Mode commands
    if (strcmp(sub, "a232") == 0) {
        serial_set_mode(CHANNEL_A, COM_RS232);
        uart_init_a();
        Serial.println("Ch A → RS-232 mode");
        return;
    }
    if (strcmp(sub, "a485") == 0) {
        serial_set_mode(CHANNEL_A, COM_RS485);
        uart_init_a();
        Serial.println("Ch A → RS-485 mode");
        return;
    }
    if (strcmp(sub, "b232") == 0) {
        serial_set_mode(CHANNEL_B, COM_RS232);
        uart_init_b();
        Serial.println("Ch B → RS-232 mode");
        return;
    }
    if (strcmp(sub, "b485") == 0) {
        serial_set_mode(CHANNEL_B, COM_RS485);
        uart_init_b();
        Serial.println("Ch B → RS-485 mode");
        return;
    }

    // Termination toggle
    if (strcmp(sub, "at") == 0) {
        static bool term_a = false;
        term_a = !term_a;
        serial_rs485_termination(CHANNEL_A, term_a);
        Serial.printf("Ch A termination: %s\n", term_a ? "ON" : "OFF");
        return;
    }
    if (strcmp(sub, "bt") == 0) {
        static bool term_b = false;
        term_b = !term_b;
        serial_rs485_termination(CHANNEL_B, term_b);
        Serial.printf("Ch B termination: %s\n", term_b ? "ON" : "OFF");
        return;
    }

    // Send on A
    if (sub[0] == 's' && sub[1] == 'a' && sub[2] == ' ') {
        if (!s_uart_a_init) { Serial.println("Init A first (ua232/ua485)"); return; }
        const char* msg = cmd + 4;  // skip "usa "
        // Enable DE for RS-485
        serial_rs485_transmit(CHANNEL_A, true);
        delayMicroseconds(100);
        Serial1.print(msg);
        Serial1.flush();
        delayMicroseconds(100);
        serial_rs485_transmit(CHANNEL_A, false);
        Serial.printf("Sent on A: \"%s\"\n", msg);
        return;
    }

    // Send on B
    if (sub[0] == 's' && sub[1] == 'b' && sub[2] == ' ') {
        if (!s_uart_b_init) { Serial.println("Init B first (ub232/ub485)"); return; }
        const char* msg = cmd + 4;  // skip "usb "
        serial_rs485_transmit(CHANNEL_B, true);
        delayMicroseconds(100);
        Serial2.print(msg);
        Serial2.flush();
        delayMicroseconds(100);
        serial_rs485_transmit(CHANNEL_B, false);
        Serial.printf("Sent on B: \"%s\"\n", msg);
        return;
    }

    // Listen
    if (sub[0] == 'l' && sub[1] == '\0') {
        Serial.println("Listening 5s (both channels)...");
        unsigned long end = millis() + 5000;
        while (millis() < end) {
            if (s_uart_a_init && Serial1.available()) {
                Serial.printf("  A rx: 0x%02X '%c'\n", Serial1.peek(), Serial1.peek() >= 32 ? Serial1.peek() : '.');
                Serial1.read();
            }
            if (s_uart_b_init && Serial2.available()) {
                Serial.printf("  B rx: 0x%02X '%c'\n", Serial2.peek(), Serial2.peek() >= 32 ? Serial2.peek() : '.');
                Serial2.read();
            }
            delay(1);
        }
        Serial.println("Listen done.");
        return;
    }

    // Loopback test (A→B and B→A)
    if (strncmp(sub, "loop", 4) == 0) {
        if (!s_uart_a_init || !s_uart_b_init) {
            Serial.println("Init both channels first (ua232/ua485 + ub232/ub485)");
            return;
        }
        // Flush
        while (Serial1.available()) Serial1.read();
        while (Serial2.available()) Serial2.read();

        // A → B
        Serial.println("Loopback test: A → B");
        ComMode mode_a = serial_get_mode(CHANNEL_A);
        ComMode mode_b = serial_get_mode(CHANNEL_B);
        if (mode_a == COM_RS485) serial_rs485_transmit(CHANNEL_A, true);
        delayMicroseconds(100);
        Serial1.print("HELLO");
        Serial1.flush();
        delayMicroseconds(100);
        if (mode_a == COM_RS485) serial_rs485_transmit(CHANNEL_A, false);
        delay(50);
        
        char buf[32];
        uint8_t pos = 0;
        unsigned long timeout = millis() + 200;
        while (millis() < timeout && pos < sizeof(buf) - 1) {
            if (Serial2.available()) {
                buf[pos++] = Serial2.read();
            }
        }
        buf[pos] = '\0';
        Serial.printf("  Sent on A: \"HELLO\", received on B: \"%s\" %s\n", 
                      buf, strcmp(buf, "HELLO") == 0 ? "✓" : "✗");

        // B → A
        Serial.println("Loopback test: B → A");
        while (Serial1.available()) Serial1.read();
        if (mode_b == COM_RS485) serial_rs485_transmit(CHANNEL_B, true);
        delayMicroseconds(100);
        Serial2.print("WORLD");
        Serial2.flush();
        delayMicroseconds(100);
        if (mode_b == COM_RS485) serial_rs485_transmit(CHANNEL_B, false);
        delay(50);

        pos = 0;
        timeout = millis() + 200;
        while (millis() < timeout && pos < sizeof(buf) - 1) {
            if (Serial1.available()) {
                buf[pos++] = Serial1.read();
            }
        }
        buf[pos] = '\0';
        Serial.printf("  Sent on B: \"WORLD\", received on A: \"%s\" %s\n",
                      buf, strcmp(buf, "WORLD") == 0 ? "✓" : "✗");

        return;
    }

    Serial.println("Unknown uart cmd. Send 'u' for help.");
}

static char s_cmd_buf[64];
static uint8_t s_cmd_pos = 0;

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
            case 'a': adc_read_all();           return;
            case 'd': debug_all();              return;
            case 't': debug_task_list();        return;
            case 'r': debug_runtime_stats();    return;
            case 'm': debug_memory();           return;
            case 'i': i2c_scan();               return;
            case 'v': version_cmd();            return;
            case 'f': inputs_voltage_mode();    return;
            case 'n': inputs_ma_mode();         return;
            case 'o': inputs_off();             return;
            case 'w': inputs_selftest();        return;
            case 'x':
                relay_set(RELAY_A, !relay_get(RELAY_A));
                Serial.printf("Relay A → %s\n", relay_get(RELAY_A) ? "ON" : "OFF");
                return;
            case 'y':
                relay_set(RELAY_B, !relay_get(RELAY_B));
                Serial.printf("Relay B → %s\n", relay_get(RELAY_B) ? "ON" : "OFF");
                return;
            case 'c':
                Serial.println("\n⚠ ADC ZERO CALIBRATION");
                Serial.println("  Disconnect all inputs first!");
                Serial.println("  Running...");
                adc_calibrate_zero();
                Serial.println("  Done. Offsets:");
                for (int i = 0; i < 8; i++) {
                    const char* n[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
                    Serial.printf("    %s: %d mV\n", n[i], adc_get_offset((AdcInput)i));
                }
                Serial.println();
                return;
            case 'z':
                buzzer_beep(1200, 150);
                Serial.println("Beep! (use z1-z6 for sounds)");
                return;
            case 'u':
                uart_test_cmd("");
                return;
            case '?':
                Serial.println("\nCommands (all require Enter):");
                Serial.println("  a           read all ADC");
                Serial.println("  d           full debug dump");
                Serial.println("  t           task list");
                Serial.println("  r           runtime info");
                Serial.println("  m           memory info");
                Serial.println("  i           I2C bus scan");
                Serial.println("  v           HW/module version");
                Serial.println("  f           all inputs → voltage mode");
                Serial.println("  n           all inputs → mA mode");
                Serial.println("  o           all inputs → off");
                Serial.println("  w           self-test");
                Serial.println("  x           toggle relay A");
                Serial.println("  y           toggle relay B");
                Serial.println("  c           ADC zero calibration");
                Serial.println("  z/z1-z6     buzzer sounds");
                Serial.println("  u           UART test (ua232/ub485/usa/ul/uloop)");
                Serial.println("  va0-24      channel A voltage");
                Serial.println("  vb0-24      channel B voltage");
                Serial.println("  s <name>    suspend task");
                Serial.println("  g <name>    resume task");
                Serial.println("  8/2/4/6/5   menu nav (no Enter needed)");
                Serial.println("  ?           this help\n");
                return;
        }
    }

    // Buzzer sounds: z1-z6
    if (len == 2 && lc[0] == 'z' && lc[1] >= '1' && lc[1] <= '6') {
        switch (lc[1]) {
            case '1': buzzer_sound_ok();       Serial.println("OK sound"); break;
            case '2': buzzer_sound_error();    Serial.println("Error sound"); break;
            case '3': buzzer_sound_startup();  Serial.println("Startup sound"); break;
            case '4': buzzer_sound_click();    Serial.println("Click sound"); break;
            case '5': buzzer_sound_warning();  Serial.println("Warning sound"); break;
            case '6': buzzer_sound_complete(); Serial.println("Complete sound"); break;
        }
        return;
    }

    // UART test commands: ua232, ua485, ub232, ub485, uat, ubt, usa X, usb X, ul, uloop
    if (len >= 2 && lc[0] == 'u') {
        uart_test_cmd(cmd);
        return;
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


static void serial_cmd_task(void* param) {
    (void)param;
    for (;;) {
        while (Serial.available()) {
            char c = Serial.read();

            // Skip CR/LF → process buffered command
            if (c == '\n' || c == '\r') {
                if (s_cmd_pos > 0) {
                    s_cmd_buf[s_cmd_pos] = '\0';
                    process_command(s_cmd_buf);
                    s_cmd_pos = 0;
                }
                continue;
            }

            // Menu keys respond immediately (only when not building a command)
            if (s_cmd_pos == 0) {
                switch (c) {
                    case '8': menu_handle_button(BTN_UP);    continue;
                    case '2': menu_handle_button(BTN_DOWN);  continue;
                    case '4': menu_handle_button(BTN_LEFT);  continue;
                    case '6': menu_handle_button(BTN_RIGHT); continue;
                    case '5': menu_handle_button(BTN_OK);    continue;
                }
            }

            // Buffer everything else
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
