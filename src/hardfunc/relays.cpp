#include "relays.h"
#include "hw_status.h"
#include "pins.h"
#include "hal.h"
#include "tca9535.h"

// Relays share the serial_control expander (0x27), port 0, bits 6+7
// We must read-modify-write carefully to not clobber serial_control bits.
static TCA9535 expander(ADDR_SERIAL_CONTROL);

#define RELAY_A_BIT  6
#define RELAY_B_BIT  7

static bool s_relay_a = false;
static bool s_relay_b = false;

// Declared in serial_control.cpp — shared port0 state
extern uint8_t serial_control_port0;

static void write_relays() {
    if (!i2c_take(100)) return;

    // Update relay bits in the shared port0 state
    if (s_relay_a) serial_control_port0 |= (1 << RELAY_A_BIT);
    else           serial_control_port0 &= ~(1 << RELAY_A_BIT);

    if (s_relay_b) serial_control_port0 |= (1 << RELAY_B_BIT);
    else           serial_control_port0 &= ~(1 << RELAY_B_BIT);

    expander.write_port(0, serial_control_port0);
    i2c_give();
}

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
