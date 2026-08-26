#include "relays.h"
#include "hw_status.h"
#include "pins.h"
#include "i2c.h"
#include "tca9535.h"

// Relays are on the serial_control expander (0x27), port 0, bits 6+7
static TCA9535 expander(ADDR_SERIAL_CONTROL);

#define RELAY_A_BIT  6
#define RELAY_B_BIT  7

static bool s_relay_a = false;
static bool s_relay_b = false;

static void write_relays() {
    if (!i2c_take(100)) return;

    // Read-modify-write to preserve serial_control bits
    uint8_t port0 = expander.read_output(0);

    if (s_relay_a) port0 |= (1 << RELAY_A_BIT);
    else           port0 &= ~(1 << RELAY_A_BIT);

    if (s_relay_b) port0 |= (1 << RELAY_B_BIT);
    else           port0 &= ~(1 << RELAY_B_BIT);

    expander.write_port(0, port0);
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
