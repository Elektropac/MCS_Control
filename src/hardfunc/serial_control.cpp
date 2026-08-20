#include "serial_control.h"
#include "hw_status.h"
#include "pins.h"
#include "hal.h"
#include "tca9535.h"

static TCA9535 expander(ADDR_SERIAL_CONTROL);

// Port 0 bit positions (channel A + relays)
#define CHA_FORCEOFF  0
#define CHA_DE        1
#define CHA_FORCEON   2
#define CHA_EN        3
#define CHA_RE        4
#define CHA_TERM      5

// Port 1 bit positions (channel B)
#define CHB_FORCEOFF  0
#define CHB_DE        1
#define CHB_FORCEON   2
#define CHB_EN        3
#define CHB_RE        4
#define CHB_TERM      5

uint8_t serial_control_port0 = 0x00;  // shared with relays.cpp (bits 6-7)
static uint8_t s_port1 = 0x00;

static void set_bit(uint8_t &port, uint8_t bit, bool high) {
    if (high) port |= (1 << bit);
    else      port &= ~(1 << bit);
}

void serial_control_init() {
    if (!i2c_take(100)) return;
    expander.set_port_direction(0, 0x00);
    expander.set_port_direction(1, 0x00);

    // Default: everything off (preserve relay bits 6-7)
    serial_control_port0 &= 0xC0;
    set_bit(serial_control_port0, CHA_EN, true);
    set_bit(serial_control_port0, CHA_RE, true);
    set_bit(serial_control_port0, CHA_TERM, true);

    s_port1 = 0x00;
    set_bit(s_port1, CHB_EN, true);
    set_bit(s_port1, CHB_RE, true);
    set_bit(s_port1, CHB_TERM, true);

    expander.write_port(0, serial_control_port0);
    expander.write_port(1, s_port1);
    i2c_give();
}

void serial_set_mode(SerialChannel ch, ComMode mode) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;
    if (!i2c_take(100)) return;

    if (ch == CHANNEL_A) {
        switch (mode) {
            case COM_OFF:
                set_bit(serial_control_port0, CHA_FORCEOFF, false);
                set_bit(serial_control_port0, CHA_EN, true);
                set_bit(serial_control_port0, CHA_FORCEON, false);
                set_bit(serial_control_port0, CHA_DE, false);
                set_bit(serial_control_port0, CHA_RE, true);
                break;
            case COM_RS232:
                set_bit(serial_control_port0, CHA_FORCEOFF, true);
                set_bit(serial_control_port0, CHA_EN, false);
                set_bit(serial_control_port0, CHA_FORCEON, true);
                set_bit(serial_control_port0, CHA_DE, false);
                set_bit(serial_control_port0, CHA_RE, true);
                break;
            case COM_RS485:
                set_bit(serial_control_port0, CHA_FORCEOFF, false);
                set_bit(serial_control_port0, CHA_EN, true);
                set_bit(serial_control_port0, CHA_FORCEON, false);
                set_bit(serial_control_port0, CHA_DE, false);
                set_bit(serial_control_port0, CHA_RE, false);
                break;
        }
        expander.write_port(0, serial_control_port0);
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
        expander.write_port(1, s_port1);
    }
    i2c_give();
}

void serial_rs485_transmit(SerialChannel ch, bool enable) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;
    if (!i2c_take(100)) return;

    if (ch == CHANNEL_A) {
        set_bit(serial_control_port0, CHA_DE, enable);
        set_bit(serial_control_port0, CHA_RE, enable);
        expander.write_port(0, serial_control_port0);
    } else {
        set_bit(s_port1, CHB_DE, enable);
        set_bit(s_port1, CHB_RE, enable);
        expander.write_port(1, s_port1);
    }
    i2c_give();
}

void serial_rs485_termination(SerialChannel ch, bool enable) {
    if (!hw_available(HW_SERIAL_CONTROL)) return;
    if (!i2c_take(100)) return;

    if (ch == CHANNEL_A) {
        set_bit(serial_control_port0, CHA_TERM, !enable);
        expander.write_port(0, serial_control_port0);
    } else {
        set_bit(s_port1, CHB_TERM, !enable);
        expander.write_port(1, s_port1);
    }
    i2c_give();
}

// --- Query functions ---

ComMode serial_get_mode(SerialChannel ch) {
    if (ch == CHANNEL_A) {
        bool forceon = (serial_control_port0 >> CHA_FORCEON) & 1;
        bool en = (serial_control_port0 >> CHA_EN) & 1;
        if (forceon && !en) return COM_RS232;
        if (!forceon && en) {
            bool re = (serial_control_port0 >> CHA_RE) & 1;
            if (!re) return COM_RS485;
        }
        return COM_OFF;
    } else {
        bool forceon = (s_port1 >> CHB_FORCEON) & 1;
        bool en = (s_port1 >> CHB_EN) & 1;
        if (forceon && !en) return COM_RS232;
        if (!forceon && en) {
            bool re = (s_port1 >> CHB_RE) & 1;
            if (!re) return COM_RS485;
        }
        return COM_OFF;
    }
}

bool serial_get_de(SerialChannel ch) {
    if (ch == CHANNEL_A) return (serial_control_port0 >> CHA_DE) & 1;
    return (s_port1 >> CHB_DE) & 1;
}

bool serial_get_re(SerialChannel ch) {
    // RE is active low in the register
    if (ch == CHANNEL_A) return !((serial_control_port0 >> CHA_RE) & 1);
    return !((s_port1 >> CHB_RE) & 1);
}

bool serial_get_termination(SerialChannel ch) {
    // TERM is active low in register
    if (ch == CHANNEL_A) return !((serial_control_port0 >> CHA_TERM) & 1);
    return !((s_port1 >> CHB_TERM) & 1);
}
