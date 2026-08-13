#pragma once
// =======================================================
// INPUT CONFIG — analog front-end switch control
// =======================================================
//
// Each input (A1–A4, B1–B4) has 4 independent switches:
//   ANALOG  — connect to ADC for voltage measurement
//   SHUNT   — insert shunt resistor for mA measurement
//   PULLUP  — enable pull-up (for digital input or weak SSR drive)
//   DIGITAL — connect to digital input path
//
// Switches can be combined freely:
//   ANALOG + SHUNT        = measure 4-20mA
//   ANALOG alone          = measure voltage
//   DIGITAL + PULLUP      = digital input with pull-up
//   DIGITAL alone         = digital input floating
//   PULLUP or SHUNT alone = weak output (drive SSR)
//
// Controlled via two TCA9535 IO expanders:
//   Channel A = 0x25
//   Channel B = 0x23
//
// =======================================================
#include <Arduino.h>

// Input identifiers
enum Input : uint8_t {
    INPUT_A1, INPUT_A2, INPUT_A3, INPUT_A4,
    INPUT_B1, INPUT_B2, INPUT_B3, INPUT_B4,
};

// Individual switches
enum InputSwitch : uint8_t {
    SW_ANALOG,
    SW_SHUNT,
    SW_PULLUP,
    SW_DIGITAL,
};

// Convenience modes (common combinations)
enum InputMode : uint8_t {
    MODE_OFF,           // all switches off
    MODE_VOLTAGE,       // ANALOG only — measure volts
    MODE_MA,            // ANALOG + SHUNT — measure 4-20mA
    MODE_DIGITAL,       // DIGITAL only — no pull-up
    MODE_DIGITAL_PU,    // DIGITAL + PULLUP
    MODE_SSR_PULLUP,    // PULLUP only — weak drive for SSR
    MODE_SSR_SHUNT,     // SHUNT only — weak drive for SSR
};

// Initialize both expanders (call after i2c_init)
void input_config_init();

// Set individual switch on/off for a specific input
void input_config_set(Input input, InputSwitch sw, bool on);

// Get current state of a switch
bool input_config_get(Input input, InputSwitch sw);

// Set a convenience mode (clears all switches first, then sets the combo)
void input_config_mode(Input input, InputMode mode);
