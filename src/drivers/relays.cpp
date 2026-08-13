#include "relays.h"
#include "hw_status.h"
#include <Wire.h>

// ------------------------------------------
// Shared TCA9535 at 0x27 — relays on port 0
// P06 = Relay A, P07 = Relay B
//
// NOTE: This driver shares the chip with serial_control.
// Both maintain a mirror of port 0. To avoid conflicts,
// relay writes only touch bits 6 and 7.
// ------------------------------------------
#define SERIAL_EXPANDER_ADDR  0x27
#define REG_OUTPUT_0          0x02

#define RELAY_A_BIT  6
#define RELAY_B_BIT  7

// ------------------------------------------
// State
// ------------------------------------------
static bool s_relay_a = false;
static bool s_relay_b = false;

// ------------------------------------------
// Helpers
// ------------------------------------------
static void write_relays() {
    // Read current port 0 state to preserve serial_control bits
    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_OUTPUT_0);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)SERIAL_EXPANDER_ADDR, (uint8_t)1);
    uint8_t port0 = Wire.read();

    // Modify only relay bits
    if (s_relay_a) port0 |= (1 << RELAY_A_BIT);
    else           port0 &= ~(1 << RELAY_A_BIT);

    if (s_relay_b) port0 |= (1 << RELAY_B_BIT);
    else           port0 &= ~(1 << RELAY_B_BIT);

    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_OUTPUT_0);
    Wire.write(port0);
    Wire.endTransmission();
}

// ------------------------------------------
// Public API
// ------------------------------------------

void relays_init() {
    s_relay_a = false;
    s_relay_b = false;
    write_relays();
}

void relay_set(Relay relay, bool on) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;

    if (relay == RELAY_A) s_relay_a = on;
    else                  s_relay_b = on;
    write_relays();
}

bool relay_get(Relay relay) {
    if (relay == RELAY_A) return s_relay_a;
    return s_relay_b;
}
