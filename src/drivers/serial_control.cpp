#include "serial_control.h"
#include "hw_status.h"
#include <Wire.h>

// ------------------------------------------
// TCA9535 at address 0x27
// Shared with relays (P06, P07)
// ------------------------------------------
#define SERIAL_EXPANDER_ADDR  0x27

#define REG_OUTPUT_0    0x02
#define REG_OUTPUT_1    0x03
#define REG_CONFIG_0    0x06
#define REG_CONFIG_1    0x07

// ------------------------------------------
// Port 0 bit positions (channel A + relays)
// ------------------------------------------
#define CHA_FORCEOFF    0   // P00 - MAX3221 FORCEOFF# (active low)
#define CHA_DE          1   // P01 - MAX485 DE (transmit enable)
#define CHA_FORCEON     2   // P02 - MAX3221 FORCEON
#define CHA_EN          3   // P03 - MAX3221 EN# (active low)
#define CHA_RE          4   // P04 - MAX485 RE# (active low)
#define CHA_TERM        5   // P05 - RS485 termination (active low)
#define RELAY_A         6   // P06
#define RELAY_B         7   // P07

// ------------------------------------------
// Port 1 bit positions (channel B)
// ------------------------------------------
#define CHB_FORCEOFF    0   // P10 - MAX3221 FORCEOFF# (active low)
#define CHB_DE          1   // P11 - MAX485 DE (transmit enable)
#define CHB_FORCEON     2   // P12 - MAX3221 FORCEON
#define CHB_EN          3   // P13 - MAX3221 EN# (active low)
#define CHB_RE          4   // P14 - MAX485 RE# (active low)
#define CHB_TERM        5   // P15 - RS485 termination (active low)

// ------------------------------------------
// State: mirror of output registers
// ------------------------------------------
static uint8_t s_port0 = 0x00;
static uint8_t s_port1 = 0x00;

// ------------------------------------------
// Low-level I2C
// ------------------------------------------
static void write_port0() {
    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_OUTPUT_0);
    Wire.write(s_port0);
    Wire.endTransmission();
}

static void write_port1() {
    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_OUTPUT_1);
    Wire.write(s_port1);
    Wire.endTransmission();
}

// ------------------------------------------
// Helpers
// ------------------------------------------
static void set_bit(uint8_t &port, uint8_t bit, bool high) {
    if (high) port |= (1 << bit);
    else      port &= ~(1 << bit);
}

// ------------------------------------------
// Public API
// ------------------------------------------

void serial_control_init() {
    // Both ports as outputs (except P16, P17 = NC, but set as output anyway)
    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_CONFIG_0);
    Wire.write(0x00);  // port 0 = all output
    Wire.endTransmission();

    Wire.beginTransmission(SERIAL_EXPANDER_ADDR);
    Wire.write(REG_CONFIG_1);
    Wire.write(0x00);  // port 1 = all output
    Wire.endTransmission();

    // Default: everything off
    // MAX3221: FORCEOFF# = LOW (forced off), EN# = HIGH (disabled), FORCEON = LOW
    // MAX485: DE = LOW (no transmit), RE# = HIGH (no receive)
    // Termination: HIGH (disabled, active low)
    s_port0 = 0x00;
    set_bit(s_port0, CHA_FORCEOFF, false);  // FORCEOFF# low = forced off
    set_bit(s_port0, CHA_EN, true);         // EN# high = disabled
    set_bit(s_port0, CHA_RE, true);         // RE# high = no receive
    set_bit(s_port0, CHA_TERM, true);       // termination disabled

    s_port1 = 0x00;
    set_bit(s_port1, CHB_FORCEOFF, false);
    set_bit(s_port1, CHB_EN, true);
    set_bit(s_port1, CHB_RE, true);
    set_bit(s_port1, CHB_TERM, true);

    write_port0();
    write_port1();
}

void serial_set_mode(SerialChannel ch, ComMode mode) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;

    if (ch == CHANNEL_A) {
        switch (mode) {
            case COM_OFF:
                set_bit(s_port0, CHA_FORCEOFF, false);  // force off
                set_bit(s_port0, CHA_EN, true);         // disable
                set_bit(s_port0, CHA_FORCEON, false);
                set_bit(s_port0, CHA_DE, false);        // no transmit
                set_bit(s_port0, CHA_RE, true);         // no receive
                break;
            case COM_RS232:
                set_bit(s_port0, CHA_FORCEOFF, true);   // not forced off
                set_bit(s_port0, CHA_EN, false);        // enable
                set_bit(s_port0, CHA_FORCEON, true);    // force on
                set_bit(s_port0, CHA_DE, false);        // disable 485 TX
                set_bit(s_port0, CHA_RE, true);         // disable 485 RX
                break;
            case COM_RS485:
                set_bit(s_port0, CHA_FORCEOFF, false);  // force 232 off
                set_bit(s_port0, CHA_EN, true);         // disable 232
                set_bit(s_port0, CHA_FORCEON, false);
                set_bit(s_port0, CHA_DE, false);        // start in receive mode
                set_bit(s_port0, CHA_RE, false);        // enable receive
                break;
        }
        write_port0();
    } else {
        switch (mode) {
            case COM_OFF:
                set_bit(s_port1, CHB_FORCEOFF, false);
                set_bit(s_port1, CHB_EN, true);
                set_bit(s_port1, CHB_FORCEON, false);
                set_bit(s_port1, CHB_DE, false);
                set_bit(s_port1, CHB_RE, true);
                break;
            case COM_RS232:
                set_bit(s_port1, CHB_FORCEOFF, true);
                set_bit(s_port1, CHB_EN, false);
                set_bit(s_port1, CHB_FORCEON, true);
                set_bit(s_port1, CHB_DE, false);
                set_bit(s_port1, CHB_RE, true);
                break;
            case COM_RS485:
                set_bit(s_port1, CHB_FORCEOFF, false);
                set_bit(s_port1, CHB_EN, true);
                set_bit(s_port1, CHB_FORCEON, false);
                set_bit(s_port1, CHB_DE, false);
                set_bit(s_port1, CHB_RE, false);
                break;
        }
        write_port1();
    }
}

void serial_rs485_transmit(SerialChannel ch, bool enable) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;

    if (ch == CHANNEL_A) {
        set_bit(s_port0, CHA_DE, enable);    // DE high = transmit
        set_bit(s_port0, CHA_RE, enable);    // RE# high = disable receive while transmitting
        write_port0();
    } else {
        set_bit(s_port1, CHB_DE, enable);
        set_bit(s_port1, CHB_RE, enable);
        write_port1();
    }
}

void serial_rs485_termination(SerialChannel ch, bool enable) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;

    if (ch == CHANNEL_A) {
        set_bit(s_port0, CHA_TERM, !enable);  // active low
        write_port0();
    } else {
        set_bit(s_port1, CHB_TERM, !enable);
        write_port1();
    }
}
