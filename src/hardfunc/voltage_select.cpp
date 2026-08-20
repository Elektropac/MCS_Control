// =======================================================
// VOLTAGE SELECT — 2-channel isolated DC-DC output
// =======================================================
// Hardware: TCA9535 (0x21) Port 0 → SN74HC139DBR (2-to-4 demux) → ROE-0505S DC-DC
//
// Each channel has 3 isolated DC-DC converters (5V, 12V, 24V).
// The 74HC139 demux selects which one is enabled (active low outputs).
// Only one output per channel is active at a time.
//
// --- CHANNEL A (Port 0, bits 0-2) ---
//   P00 = 1G# (Enable, active low)
//   P01 = 1A  (SetVoltage1_A / address bit 0)
//   P02 = 1B  (SetVoltage2_A / address bit 1)
//
//   74HC139 decoder 1:
//   1B  1A  → Active output
//    0   1  → 1Y1 = CONTROL_A_5V
//    1   0  → 1Y2 = CONTROL_A_24V
//    1   1  → 1Y3 = CONTROL_A_12V
//    0   0  → 1Y0 (unused)
//
// --- CHANNEL B (Port 0, bits 5-7) ---
//   P05 = 2B  (SetVoltage2_B / address bit 1)
//   P06 = 2A  (SetVoltage1_B / address bit 0)
//   P07 = 2G# (EnableVoltage_B, active low)
//
//   NOTE: Kanal B has enable on MSB (P07) unlike kanal A (P00).
//         This is a PCB routing decision.
//
//   74HC139 decoder 2:
//   2B  2A  → Active output
//    0   1  → 2Y1 = CONTROL_B_5V
//    1   0  → 2Y2 = CONTROL_B_24V
//    1   1  → 2Y3 = CONTROL_B_12V
//    0   0  → 2Y0 (unused)
//
// --- Port 0 bit summary ---
//   Bit 0: 1G# (ch A enable)
//   Bit 1: 1A  (ch A sel0)
//   Bit 2: 1B  (ch A sel1)
//   Bit 3: (unused)
//   Bit 4: (unused)
//   Bit 5: 2B  (ch B sel1)
//   Bit 6: 2A  (ch B sel0)
//   Bit 7: 2G# (ch B enable)
//
// --- DC-DC: RECOM ROE-0505S (unregulated, isolated) ---
//   Max load: 85 mA per channel
//
//   Test results (2026-08-19, kanal B, electronic load):
//     Load     5V rail    12V rail    24V rail
//     0 mA     6.99 V     15.03 V     29.87 V
//    50 mA     5.43 V     12.67 V     25.14 V
//   100 mA     5.29 V     12.02 V     23.65 V
//
//   Unregulated outputs — voltage drops with load, rises unloaded.
//   This is normal for ROE-series. Kanal A is identical circuit.
// =======================================================

#include "voltage_select.h"
#include "hw_status.h"
#include "pins.h"
#include "hal.h"
#include "tca9535.h"

static TCA9535 expander(ADDR_VOLTAGE_SELECT);
static uint8_t s_port0 = 0xFF;
static Voltage s_voltage_a = VOLTAGE_OFF;
static Voltage s_voltage_b = VOLTAGE_OFF;

void voltage_select_init() {
    if (!i2c_take(100)) return;
    expander.set_port_direction(0, 0x00);  // port 0 = output
    expander.set_port_direction(1, 0xFF);  // port 1 = input (version)
    s_port0 = 0xFF;
    expander.write_port(0, s_port0);
    i2c_give();
}

void voltage_select_set_a(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;
    if (!i2c_take(100)) return;

    s_voltage_a = v;
    s_port0 |= 0x07;  // disable (P00=1, P01=1, P02=1)

    if (v != VOLTAGE_OFF) {
        uint8_t sel = 0;
        switch (v) {
            case VOLTAGE_24V: sel = 0b01; break;
            case VOLTAGE_12V: sel = 0b10; break;
            case VOLTAGE_5V:  sel = 0b11; break;
            default: break;
        }
        s_port0 = (s_port0 & ~0x06) | ((sel & 0x03) << 1);
        s_port0 &= ~0x01;  // enable (P00=0)
    }

    expander.write_port(0, s_port0);
    i2c_give();
}

void voltage_select_set_b(Voltage v) {
    if (!hw_available(HW_VOLTAGE_SELECT)) return;
    if (!i2c_take(100)) return;

    s_voltage_b = v;
    s_port0 |= 0xE0;  // disable (P05=1, P06=1, P07=1)

    if (v != VOLTAGE_OFF) {
        uint8_t p05, p06;
        switch (v) {
            case VOLTAGE_5V:  p05 = 0; p06 = 1; break;  // 2B=0, 2A=1 → 2Y1
            case VOLTAGE_24V: p05 = 1; p06 = 0; break;  // 2B=1, 2A=0 → 2Y2
            case VOLTAGE_12V: p05 = 1; p06 = 1; break;  // 2B=1, 2A=1 → 2Y3
            default: p05 = 0; p06 = 0; break;
        }
        s_port0 = (s_port0 & ~0xE0) | (p05 << 5) | (p06 << 6) | (0 << 7);  // P07=0 = enable
    }

    expander.write_port(0, s_port0);
    i2c_give();
}

Voltage voltage_select_get_a() { return s_voltage_a; }
Voltage voltage_select_get_b() { return s_voltage_b; }
