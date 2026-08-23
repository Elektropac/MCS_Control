#include "test_api.h"
#include "hal.h"
#include "../pins.h"
#include "tca9535.h"
#include "ads1115.h"
#include "buzzer.h"
#include "voltage_select.h"
#include "relays.h"
#include "adc.h"
#include "input_config.h"
#include "serial_control.h"
#include "version.h"
#include "../tasks/sampler.h"

namespace test_api {

static String i2c_scan_json() {
    struct { uint8_t addr; const char* name; } known[] = {
        { ADDR_VOLTAGE_SELECT, "TCA9535 Voltage/Version" },
        { ADDR_INPUT_CONFIG_B, "TCA9535 Input Config B" },
        { ADDR_INPUT_CONFIG_A, "TCA9535 Input Config A" },
        { ADDR_SERIAL_CONTROL, "TCA9535 Serial/Relay" },
        { ADDR_ADC_A,          "ADS1115 ADC-A" },
        { ADDR_ADC_B,          "ADS1115 ADC-B" },
    };

    JsonDocument doc;
    JsonArray devices = doc["devices"].to<JsonArray>();
    int found = 0;

    for (auto& dev : known) {
        bool online = i2c_probe(dev.addr);
        JsonObject obj = devices.add<JsonObject>();
        obj["addr"] = String("0x") + String(dev.addr, HEX);
        obj["name"] = dev.name;
        obj["online"] = online;
        if (online) found++;
    }
    doc["found"] = found;
    doc["total"] = 6;

    String result;
    serializeJson(doc, result);
    return result;
}

static String adc_read_json() {
    const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
    JsonDocument doc;
    JsonArray channels = doc["channels"].to<JsonArray>();

    for (int i = 0; i < 8; i++) {
        int32_t mv = adc_read_mv((AdcInput)i);
        JsonObject obj = channels.add<JsonObject>();
        obj["name"] = names[i];
        obj["mv"] = mv;
        obj["volt"] = String(mv / 1000.0, 3);
    }

    String result;
    serializeJson(doc, result);
    return result;
}

static String relay_toggle_json(JsonDocument& json_packet) {
    String which = json_packet["data"]["relay"].as<String>();
    bool state;
    if (which == "A" || which == "a") {
        relay_set(RELAY_A, !relay_get(RELAY_A));
        state = relay_get(RELAY_A);
    } else {
        relay_set(RELAY_B, !relay_get(RELAY_B));
        state = relay_get(RELAY_B);
    }
    JsonDocument doc;
    doc["relay"] = which;
    doc["state"] = state ? "ON" : "OFF";
    String result;
    serializeJson(doc, result);
    return result;
}

static String voltage_set_json(JsonDocument& json_packet) {
    String channel = json_packet["data"]["channel"].as<String>();
    int val = json_packet["data"]["voltage"].as<int>();
    Voltage volt;
    switch (val) {
        case 0:  volt = VOLTAGE_OFF; break;
        case 5:  volt = VOLTAGE_5V;  break;
        case 12: volt = VOLTAGE_12V; break;
        case 24: volt = VOLTAGE_24V; break;
        default: return "{\"error\":\"invalid voltage\"}";
    }
    if (channel == "A" || channel == "a") {
        voltage_select_set_a(volt);
    } else {
        voltage_select_set_b(volt);
    }
    JsonDocument doc;
    doc["channel"] = channel;
    doc["voltage"] = val;
    String result;
    serializeJson(doc, result);
    return result;
}

static String buzzer_test_json(JsonDocument& json_packet) {
    int sound = json_packet["data"]["sound"].as<int>();
    switch (sound) {
        case 1: buzzer_sound_ok();       break;
        case 2: buzzer_sound_error();    break;
        case 3: buzzer_sound_startup();  break;
        case 4: buzzer_sound_click();    break;
        case 5: buzzer_sound_warning();  break;
        case 6: buzzer_sound_complete(); break;
        default: buzzer_beep(1200, 150); break;
    }
    return "{\"result\":\"buzzer played\"}";
}

static String version_json() {
    JsonDocument doc;
    doc["hardware"] = version_hardware();
    doc["module"] = version_module();
    String result;
    serializeJson(doc, result);
    return result;
}

static String selftest_json(JsonDocument& json_packet) {
    bool verbose = false;
    String section = "all";
    if (!json_packet["data"].isNull()) {
        verbose = json_packet["data"]["verbose"].as<bool>();
        if (!json_packet["data"]["section"].isNull())
            section = json_packet["data"]["section"].as<String>();
    }

    const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
    const uint8_t dig_pins[] = {A1_DIGITAL, A2_DIGITAL, A3_DIGITAL, A4_DIGITAL,
                                B1_DIGITAL, B2_DIGITAL, B3_DIGITAL, B4_DIGITAL};

    for (int i = 0; i < 8; i++) pinMode(dig_pins[i], INPUT);

    JsonDocument doc;
    JsonArray tests = doc["tests"].to<JsonArray>();
    int pass = 0, fail = 0;

    // Helper: add test result
    auto addTest = [&](const char* test_name, int32_t value, const char* unit, bool ok) {
        if (ok) pass++; else fail++;
        if (!ok || verbose) {
            JsonObject t = tests.add<JsonObject>();
            t["test"] = test_name;
            t["value"] = value;
            if (unit) t["unit"] = unit;
            t["pass"] = ok;
        }
    };

    // --- I2C ---
    if (section == "all" || section == "i2c") {
        struct { uint8_t addr; const char* name; } known[] = {
            { ADDR_VOLTAGE_SELECT, "Voltage Select" },
            { ADDR_INPUT_CONFIG_B, "Input Config B" },
            { ADDR_INPUT_CONFIG_A, "Input Config A" },
            { ADDR_SERIAL_CONTROL, "Serial/Relay" },
            { ADDR_ADC_A,          "ADC-A" },
            { ADDR_ADC_B,          "ADC-B" },
        };
        for (auto& dev : known) {
            bool online = i2c_probe(dev.addr);
            String tname = String("I2C ") + dev.name;
            addTest(tname.c_str(), online ? 1 : 0, nullptr, online);
        }
    }

    // --- Analog ---
    if (section == "all" || section == "analog") {
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
        for (int i = 0; i < 8; i++) {
            int32_t mv = adc_read_mv((AdcInput)i);
            String tname = String("Pullup ") + names[i];
            addTest(tname.c_str(), mv, "mV", mv > 3000 && mv < 6000);
        }
        for (int i = 0; i < 8; i++) input_config_set((Input)i, SW_SHUNT, true);
        delay(50);
        for (int i = 0; i < 8; i++) {
            int32_t mv = adc_read_mv((AdcInput)i);
            String tname = String("Shunt ") + names[i];
            addTest(tname.c_str(), mv, "mV", mv > 50 && mv < 500);
        }
        for (int i = 0; i < 8; i++) input_config_mode((Input)i, MODE_OFF);
        voltage_select_set_a(VOLTAGE_OFF);
        voltage_select_set_b(VOLTAGE_OFF);
    }

    // --- Supply ---
    if (section == "all" || section == "supply") {
        Voltage supply_levels[] = {VOLTAGE_5V, VOLTAGE_12V, VOLTAGE_24V};
        const char* supply_names[] = {"5V", "12V", "24V"};
        int32_t expected_min[] = {4000, 10000, 20000};
        int32_t expected_max[] = {8000, 18000, 35000};

        for (int v = 0; v < 3; v++) {
            voltage_select_set_a(supply_levels[v]);
            voltage_select_set_b(supply_levels[v]);
            delay(50);
            for (int i = 0; i < 8; i++) {
                input_config_set((Input)i, SW_ANALOG, true);
                input_config_set((Input)i, SW_PULLUP, true);
                input_config_set((Input)i, SW_SHUNT, true);
                input_config_set((Input)i, SW_DIGITAL, false);
                delay(20);
                int32_t mv = adc_read_mv((AdcInput)i);
                int32_t supply_mv = mv * 26;
                String tname = String(supply_names[v]) + " " + names[i];
                addTest(tname.c_str(), supply_mv, "mV", supply_mv > expected_min[v] && supply_mv < expected_max[v]);
                input_config_set((Input)i, SW_ANALOG, false);
                input_config_set((Input)i, SW_PULLUP, false);
                input_config_set((Input)i, SW_SHUNT, false);
            }
        }
        voltage_select_set_a(VOLTAGE_OFF);
        voltage_select_set_b(VOLTAGE_OFF);
    }

    // --- Digital ---
    if (section == "all" || section == "digital") {
        Voltage test_volts[] = {VOLTAGE_5V, VOLTAGE_12V, VOLTAGE_24V};
        const char* volt_names[] = {"5V", "12V", "24V"};

        for (int v = 0; v < 3; v++) {
            voltage_select_set_a(test_volts[v]);
            voltage_select_set_b(test_volts[v]);
            delay(10);
            for (int i = 0; i < 8; i++) {
                input_config_set((Input)i, SW_ANALOG, false);
                input_config_set((Input)i, SW_PULLUP, true);
                input_config_set((Input)i, SW_SHUNT, false);
                input_config_set((Input)i, SW_DIGITAL, true);
            }
            delay(50);
            for (int i = 0; i < 8; i++) {
                int dig = digitalRead(dig_pins[i]);
                String tname = String(volt_names[v]) + " HIGH " + names[i];
                addTest(tname.c_str(), dig, nullptr, dig == LOW);
            }
            for (int i = 0; i < 8; i++) {
                input_config_set((Input)i, SW_PULLUP, false);
                input_config_set((Input)i, SW_SHUNT, true);
            }
            delay(50);
            for (int i = 0; i < 8; i++) {
                int dig = digitalRead(dig_pins[i]);
                String tname = String(volt_names[v]) + " LOW " + names[i];
                addTest(tname.c_str(), dig, nullptr, dig == HIGH);
            }
        }
        for (int i = 0; i < 8; i++) input_config_mode((Input)i, MODE_OFF);
        voltage_select_set_a(VOLTAGE_OFF);
        voltage_select_set_b(VOLTAGE_OFF);
    }

    doc["pass"] = pass;
    doc["fail"] = fail;
    doc["total"] = pass + fail;
    doc["section"] = section;

    String result;
    serializeJson(doc, result);
    return result;
}

static String uart_loopback_json(JsonDocument& json_packet) {
    String mode = json_packet["data"]["mode"].as<String>(); // "232" or "485"
    ComMode com_mode = (mode == "485") ? COM_RS485 : COM_RS232;

    serial_set_mode(CHANNEL_A, com_mode);
    serial_set_mode(CHANNEL_B, com_mode);
    Serial1.begin(9600, SERIAL_8N1, UART_A_RX, UART_A_TX);
    Serial2.begin(9600, SERIAL_8N1, UART_B_RX, UART_B_TX);
    delay(50);

    // Flush
    while (Serial1.available()) Serial1.read();
    while (Serial2.available()) Serial2.read();

    JsonDocument doc;
    
    // A → B
    if (com_mode == COM_RS485) serial_rs485_transmit(CHANNEL_A, true);
    delayMicroseconds(100);
    Serial1.print("HELLO");
    Serial1.flush();
    delayMicroseconds(100);
    if (com_mode == COM_RS485) serial_rs485_transmit(CHANNEL_A, false);
    delay(50);

    char buf[32];
    uint8_t pos = 0;
    unsigned long timeout = millis() + 200;
    while (millis() < timeout && pos < sizeof(buf) - 1) {
        if (Serial2.available()) buf[pos++] = Serial2.read();
    }
    buf[pos] = '\0';
    doc["a_to_b"]["sent"] = "HELLO";
    doc["a_to_b"]["received"] = buf;
    doc["a_to_b"]["pass"] = (strcmp(buf, "HELLO") == 0);

    // B → A
    while (Serial1.available()) Serial1.read();
    if (com_mode == COM_RS485) serial_rs485_transmit(CHANNEL_B, true);
    delayMicroseconds(100);
    Serial2.print("WORLD");
    Serial2.flush();
    delayMicroseconds(100);
    if (com_mode == COM_RS485) serial_rs485_transmit(CHANNEL_B, false);
    delay(50);

    pos = 0;
    timeout = millis() + 200;
    while (millis() < timeout && pos < sizeof(buf) - 1) {
        if (Serial1.available()) buf[pos++] = Serial1.read();
    }
    buf[pos] = '\0';
    doc["b_to_a"]["sent"] = "WORLD";
    doc["b_to_a"]["received"] = buf;
    doc["b_to_a"]["pass"] = (strcmp(buf, "WORLD") == 0);

    doc["mode"] = mode == "485" ? "RS-485" : "RS-232";

    String result;
    serializeJson(doc, result);
    return result;
}

static String pulse_test_json(JsonDocument& json_packet) {
    // Pulse test: toggle pullup/shunt on A-side, count edges on B-side via sampler
    // Requires physical wiring: A1→B1, A2→B2, A3→B3, A4→B4
    // Tests at 5V, 12V, 24V
    
    int num_pulses = 10;
    if (!json_packet["data"].isNull() && !json_packet["data"]["pulses"].isNull())
        num_pulses = json_packet["data"]["pulses"].as<int>();

    const char* a_names[] = {"A1","A2","A3","A4"};
    const char* b_names[] = {"B1","B2","B3","B4"};
    Voltage test_volts[] = {VOLTAGE_5V, VOLTAGE_12V, VOLTAGE_24V};
    const char* volt_names[] = {"5V", "12V", "24V"};

    JsonDocument doc;
    JsonArray results = doc["results"].to<JsonArray>();
    int pass = 0, fail = 0;

    for (int v = 0; v < 3; v++) {
        // Set voltage on both channels
        voltage_select_set_a(test_volts[v]);
        voltage_select_set_b(test_volts[v]);
        delay(20);

        // A→B: toggle A-side, measure B-side
        for (int ch = 0; ch < 4; ch++) {
            Input a_input = (Input)ch;       // A1-A4 = 0-3
            int b_bit = ch + 4;              // B1-B4 = bit 4-7 in sampler

            // Setup A-side: digital ON, start LOW (pullup OFF, shunt ON)
            input_config_set(a_input, SW_ANALOG, false);
            input_config_set(a_input, SW_DIGITAL, true);
            input_config_set(a_input, SW_PULLUP, false);
            input_config_set(a_input, SW_SHUNT, true);

            // Setup B-side: digital input enabled
            Input b_input = (Input)(ch + 4);
            input_config_set(b_input, SW_ANALOG, false);
            input_config_set(b_input, SW_DIGITAL, true);
            input_config_set(b_input, SW_PULLUP, false);
            input_config_set(b_input, SW_SHUNT, false);
            delay(10);

            // Record sampler position and initial state before test
            uint8_t initial_state = sampler_current_state();
            uint32_t cursor = sampler_write_position();

            // Generate pulses
            // Fast: single I2C write per toggle (pullup+shunt in one port write)
            // We write directly to the expander, bypassing input_config_set
            TCA9535 exp_direct(ADDR_INPUT_CONFIG_A);
            const uint8_t port = (ch < 2) ? 1 : 0; // A1,A2 on port1; A3,A4 on port0
            // Build bitmasks: keep digital bit ON, toggle pullup vs shunt
            uint8_t digital_bit, pullup_bit, shunt_bit;
            switch (ch) {
                case 0: digital_bit=0; pullup_bit=2; shunt_bit=1; break; // A1 port1
                case 1: digital_bit=4; pullup_bit=6; shunt_bit=5; break; // A2 port1
                case 2: digital_bit=7; pullup_bit=5; shunt_bit=6; break; // A3 port0
                case 3: digital_bit=3; pullup_bit=1; shunt_bit=2; break; // A4 port0
            }
            uint8_t val_high = (1 << digital_bit) | (1 << pullup_bit);
            uint8_t val_low  = (1 << digital_bit) | (1 << shunt_bit);

            if (i2c_take(100)) {
                for (int p = 0; p < num_pulses; p++) {
                    exp_direct.write_port(port, val_high);
                    delayMicroseconds(400);
                    exp_direct.write_port(port, val_low);
                    delayMicroseconds(400);
                }
                i2c_give();
            }
            delay(5);

            // Count edges on B-channel
            uint32_t rising = 0, falling = 0;
            uint32_t first_edge_idx = 0, last_edge_idx = 0;
            SamplerEvent evt;
            uint8_t prev_bit = (initial_state >> b_bit) & 1;
            while (sampler_read_next(cursor, evt)) {
                uint8_t bit_val = (evt.state >> b_bit) & 1;
                if (bit_val != prev_bit) {
                    if (bit_val == 1) rising++;
                    else falling++;
                    if (first_edge_idx == 0) first_edge_idx = evt.sample_index;
                    last_edge_idx = evt.sample_index;
                }
                prev_bit = bit_val;
            }

            uint32_t edges = rising + falling;
            uint32_t expected = num_pulses * 2;
            bool ok = (edges >= expected - 2 && edges <= expected + 2);

            float period_ms = 0, freq_hz = 0;
            if (edges > 1) {
                uint32_t span = last_edge_idx - first_edge_idx;
                float span_ms = span * 0.25f;
                period_ms = span_ms / (edges - 1);
                float pulse_ms = period_ms * 2;
                if (pulse_ms > 0) freq_hz = 1000.0f / pulse_ms;
            }

            JsonObject r = results.add<JsonObject>();
            r["test"] = String(volt_names[v]) + " " + a_names[ch] + "→" + b_names[ch];
            r["sent"] = num_pulses;
            r["rising"] = rising;
            r["falling"] = falling;
            r["edges"] = edges;
            r["expected"] = expected;
            r["period_ms"] = String(period_ms, 2);
            r["freq_hz"] = String(freq_hz, 1);
            r["pass"] = ok;
            if (ok) pass++; else fail++;

            input_config_mode(a_input, MODE_OFF);
            input_config_mode(b_input, MODE_OFF);
        }

        // B→A: toggle B-side, measure A-side
        for (int ch = 0; ch < 4; ch++) {
            Input b_input = (Input)(ch + 4);  // B1-B4 = 4-7
            int a_bit = ch;                   // A1-A4 = bit 0-3 in sampler

            // Setup B-side: digital ON, start LOW
            input_config_set(b_input, SW_ANALOG, false);
            input_config_set(b_input, SW_DIGITAL, true);
            input_config_set(b_input, SW_PULLUP, false);
            input_config_set(b_input, SW_SHUNT, true);

            // Setup A-side: digital input enabled
            Input a_input = (Input)ch;
            input_config_set(a_input, SW_ANALOG, false);
            input_config_set(a_input, SW_DIGITAL, true);
            input_config_set(a_input, SW_PULLUP, false);
            input_config_set(a_input, SW_SHUNT, false);
            delay(10);

            // Record sampler position and initial state
            uint8_t initial_state = sampler_current_state();
            uint32_t cursor = sampler_write_position();

            // Generate pulses on B-side (fast single I2C write)
            TCA9535 exp_direct_b(ADDR_INPUT_CONFIG_B);
            const uint8_t port_b = (ch < 2) ? 0 : 1; // B1,B2 on port0; B3,B4 on port1
            uint8_t digital_bit_b, pullup_bit_b, shunt_bit_b;
            switch (ch) {
                case 0: digital_bit_b=6; pullup_bit_b=4; shunt_bit_b=7; break; // B1 port0
                case 1: digital_bit_b=2; pullup_bit_b=0; shunt_bit_b=3; break; // B2 port0
                case 2: digital_bit_b=1; pullup_bit_b=3; shunt_bit_b=0; break; // B3 port1
                case 3: digital_bit_b=5; pullup_bit_b=7; shunt_bit_b=4; break; // B4 port1
            }
            uint8_t val_high_b = (1 << digital_bit_b) | (1 << pullup_bit_b);
            uint8_t val_low_b  = (1 << digital_bit_b) | (1 << shunt_bit_b);

            if (i2c_take(100)) {
                for (int p = 0; p < num_pulses; p++) {
                    exp_direct_b.write_port(port_b, val_high_b);
                    delayMicroseconds(400);
                    exp_direct_b.write_port(port_b, val_low_b);
                    delayMicroseconds(400);
                }
                i2c_give();
            }
            delay(5);

            // Count edges on A-channel
            uint32_t rising = 0, falling = 0;
            uint32_t first_edge_idx = 0, last_edge_idx = 0;
            SamplerEvent evt;
            uint8_t prev_bit = (initial_state >> a_bit) & 1;
            while (sampler_read_next(cursor, evt)) {
                uint8_t bit_val = (evt.state >> a_bit) & 1;
                if (bit_val != prev_bit) {
                    if (bit_val == 1) rising++;
                    else falling++;
                    if (first_edge_idx == 0) first_edge_idx = evt.sample_index;
                    last_edge_idx = evt.sample_index;
                }
                prev_bit = bit_val;
            }

            uint32_t edges = rising + falling;
            uint32_t expected = num_pulses * 2;
            bool ok = (edges >= expected - 2 && edges <= expected + 2);

            float period_ms = 0, freq_hz = 0;
            if (edges > 1) {
                uint32_t span = last_edge_idx - first_edge_idx;
                float span_ms = span * 0.25f;
                period_ms = span_ms / (edges - 1);
                float pulse_ms = period_ms * 2;
                if (pulse_ms > 0) freq_hz = 1000.0f / pulse_ms;
            }

            JsonObject r = results.add<JsonObject>();
            r["test"] = String(volt_names[v]) + " " + b_names[ch] + "→" + a_names[ch];
            r["sent"] = num_pulses;
            r["rising"] = rising;
            r["falling"] = falling;
            r["edges"] = edges;
            r["expected"] = expected;
            r["period_ms"] = String(period_ms, 2);
            r["freq_hz"] = String(freq_hz, 1);
            r["pass"] = ok;
            if (ok) pass++; else fail++;

            input_config_mode(a_input, MODE_OFF);
            input_config_mode(b_input, MODE_OFF);
        }
    }

    voltage_select_set_a(VOLTAGE_OFF);
    voltage_select_set_b(VOLTAGE_OFF);

    doc["pass"] = pass;
    doc["fail"] = fail;
    doc["total"] = pass + fail;
    doc["pulses_per_channel"] = num_pulses;

    String result;
    serializeJson(doc, result);
    return result;
}

static String io_control_json(JsonDocument& json_packet) {
    // data: { port: 0-7 (A1-A4=0-3, B1-B4=4-7), action: "..." }
    // Actions:
    //   "read_analog"    — SW_ANALOG on, read ADC (mV)
    //   "read_pullup"    — SW_ANALOG + SW_PULLUP on, read ADC
    //   "read_shunt"     — SW_ANALOG + SW_PULLUP + SW_SHUNT on, read ADC
    //   "read_digital"   — SW_DIGITAL on, read GPIO (0/1)
    //   "set_pullup_on"  — SW_PULLUP on
    //   "set_pullup_off" — SW_PULLUP off
    //   "set_shunt_on"   — SW_SHUNT on
    //   "set_shunt_off"  — SW_SHUNT off
    //   "set_digital_on" — SW_DIGITAL on
    //   "set_digital_off"— SW_DIGITAL off
    //   "set_analog_on"  — SW_ANALOG on
    //   "set_analog_off" — SW_ANALOG off
    //   "off"            — all switches off
    //   "status"         — read current switch states + ADC + digital

    const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
    const uint8_t dig_pins[] = {A1_DIGITAL, A2_DIGITAL, A3_DIGITAL, A4_DIGITAL,
                                B1_DIGITAL, B2_DIGITAL, B3_DIGITAL, B4_DIGITAL};

    int port = json_packet["data"]["port"].as<int>();
    String action = json_packet["data"]["action"].as<String>();

    if (port < 0 || port > 7) return "{\"error\":\"port must be 0-7\"}";

    Input inp = (Input)port;
    AdcInput adc_inp = (AdcInput)port;
    pinMode(dig_pins[port], INPUT);

    JsonDocument doc;
    doc["port"] = port;
    doc["name"] = names[port];
    doc["action"] = action;

    if (action == "read_analog") {
        input_config_set(inp, SW_ANALOG, true);
        delay(20);
        int32_t mv = adc_read_mv(adc_inp);
        doc["mv"] = mv;
        doc["volt"] = String(mv / 1000.0, 3);
    }
    else if (action == "read_pullup") {
        input_config_set(inp, SW_ANALOG, true);
        input_config_set(inp, SW_PULLUP, true);
        delay(20);
        int32_t mv = adc_read_mv(adc_inp);
        doc["mv"] = mv;
        doc["volt"] = String(mv / 1000.0, 3);
    }
    else if (action == "read_shunt") {
        input_config_set(inp, SW_ANALOG, true);
        input_config_set(inp, SW_PULLUP, true);
        input_config_set(inp, SW_SHUNT, true);
        delay(20);
        int32_t mv = adc_read_mv(adc_inp);
        int32_t supply_mv = mv * 26;
        doc["mv"] = mv;
        doc["supply_mv"] = supply_mv;
        doc["volt"] = String(mv / 1000.0, 3);
    }
    else if (action == "read_digital") {
        input_config_set(inp, SW_DIGITAL, true);
        delay(20);
        int dig = digitalRead(dig_pins[port]);
        doc["digital"] = dig;
        doc["state"] = dig == LOW ? "HIGH (opto active)" : "LOW (opto inactive)";
    }
    else if (action == "set_pullup_on")  { input_config_set(inp, SW_PULLUP, true);  doc["result"] = "pullup ON"; }
    else if (action == "set_pullup_off") { input_config_set(inp, SW_PULLUP, false); doc["result"] = "pullup OFF"; }
    else if (action == "set_shunt_on")   { input_config_set(inp, SW_SHUNT, true);   doc["result"] = "shunt ON"; }
    else if (action == "set_shunt_off")  { input_config_set(inp, SW_SHUNT, false);  doc["result"] = "shunt OFF"; }
    else if (action == "set_digital_on") { input_config_set(inp, SW_DIGITAL, true); doc["result"] = "digital ON"; }
    else if (action == "set_digital_off"){ input_config_set(inp, SW_DIGITAL, false);doc["result"] = "digital OFF"; }
    else if (action == "set_analog_on")  { input_config_set(inp, SW_ANALOG, true);  doc["result"] = "analog ON"; }
    else if (action == "set_analog_off") { input_config_set(inp, SW_ANALOG, false); doc["result"] = "analog OFF"; }
    else if (action == "off") {
        input_config_mode(inp, MODE_OFF);
        doc["result"] = "all switches OFF";
    }
    else if (action == "status") {
        // Read everything
        input_config_set(inp, SW_ANALOG, true);
        delay(10);
        int32_t mv = adc_read_mv(adc_inp);
        int dig = digitalRead(dig_pins[port]);
        doc["mv"] = mv;
        doc["volt"] = String(mv / 1000.0, 3);
        doc["digital"] = dig;
    }
    else {
        doc["error"] = "unknown action";
    }

    String result;
    serializeJson(doc, result);
    return result;
}

String handle(const String& function_name, JsonDocument& json_packet) {
    if (function_name == "test_i2c_scan")     return i2c_scan_json();
    if (function_name == "test_adc_read")     return adc_read_json();
    if (function_name == "test_relay")        return relay_toggle_json(json_packet);
    if (function_name == "test_voltage")      return voltage_set_json(json_packet);
    if (function_name == "test_buzzer")       return buzzer_test_json(json_packet);
    if (function_name == "test_version")      return version_json();
    if (function_name == "test_selftest")     return selftest_json(json_packet);
    if (function_name == "test_uart_loopback") return uart_loopback_json(json_packet);
    if (function_name == "test_io")           return io_control_json(json_packet);
    if (function_name == "test_pulse")       return pulse_test_json(json_packet);
    return ""; // not handled
}

} // namespace test_api
