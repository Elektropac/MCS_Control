# MCS Control — Hardware Reference

Detailed hardware documentation for developers and technicians. This document covers pin assignments, I2C bus topology, analog front-end design, power supply characteristics, and known hardware issues.

---

## Base Board

**Module:** Waveshare ESP32-S3-ETH

- ESP32-S3 dual-core Xtensa LX7, 240 MHz
- 512 KB internal SRAM
- 8 MB PSRAM (octal SPI)
- 16 MB flash
- Integrated W5500 Ethernet controller
- USB-C for programming and serial debug
- On-board 3.3 V regulator

---

## Pin Mapping

All pin definitions are in `include/pins.h`.

### Ethernet (W5500 SPI)

| Function | GPIO |
|----------|------|
| MISO | 12 |
| MOSI | 11 |
| SCK | 13 |
| CS | 14 |
| RST | 9 |
| INT | 10 |

### OLED Display (SSD1306, SPI)

| Function | GPIO |
|----------|------|
| DC | 40 |
| RST | 39 |
| CLK | 38 |
| DIN (MOSI) | 37 |
| CS | 36 |

### I2C Bus

| Function | GPIO |
|----------|------|
| SCL | 34 |
| SDA | 33 |

### UART A

| Function | GPIO |
|----------|------|
| TX | 1 |
| RX | 2 |

### UART B

| Function | GPIO |
|----------|------|
| TX | 43 |
| RX | 44 |

### Digital Inputs — Channel A

| Input | GPIO |
|-------|------|
| A0 | 48 |
| A1 | 17 |
| A2 | 16 |
| A3 | 18 |

### Digital Inputs — Channel B

| Input | GPIO |
|-------|------|
| B0 | 35 |
| B1 | 41 |
| B2 | 42 |
| B3 | 47 |

### Other

| Function | GPIO | Notes |
|----------|------|-------|
| Button | 15 | Analog ladder (5 buttons on single ADC channel) |
| Buzzer | 21 | Piezo, driven via hardware timer ISR |

---

## I2C Bus

### Configuration

- **Speed:** 400 kHz (Fast Mode)
- **Pull-ups:** 4.7 kΩ to 3.3 V (on-board)
- **Access:** Mutex-protected (FreeRTOS mutex, 100 ms timeout)
- **Devices:** 6 total

### Device Map

| Address | Device | Function | Location |
|---------|--------|----------|----------|
| 0x21 | TCA9535 | Voltage selection switches + hardware version bits | Main board |
| 0x23 | TCA9535 | Input mode switches — Channel B | Channel B board |
| 0x25 | TCA9535 | Input mode switches — Channel A | Channel A board |
| 0x27 | TCA9535 | UART mode selection + relay control | Serial/relay board |
| 0x48 | ADS1115 | 16-bit ADC — Channel A (4 inputs) | Channel A board |
| 0x49 | ADS1115 | 16-bit ADC — Channel B (4 inputs) | Channel B board |

### TCA9535 I/O Expander

- 16-bit I/O expander (8 inputs + 8 outputs, or 16 of either)
- Used for controlling CMOS switches, relays, and reading hardware version
- All outputs are latched — state is maintained until explicitly changed

---

## Input Modes

Each of the 4 inputs per channel is controlled by CMOS analog switches via the I2C expander. The switches route the input signal through different conditioning paths:

| Mode | Config | Signal Path | Use Case |
|------|--------|-------------|----------|
| SW_ANALOG | Analog voltage | Input → voltage divider → ADC | 0–5 V / 0–10 V sensors |
| SW_PULLUP | Pullup reference | Input → pullup resistor → ADC | Calibration reference (5 V) |
| SW_SHUNT | Current shunt | Input → 249 Ω to GND → ADC | 4–20 mA current loop |
| SW_DIGITAL | Digital/pulse | Input → optocoupler → GPIO | Switches, pulse counters |

### Switch Control (per input)

Each input has 4 CMOS switches controlled by 4 bits on the I2C expander. Only one switch should be active at a time (except during calibration where shunt is used for zero-reference).

---

## ADC — ADS1115

### Specifications

| Parameter | Value |
|-----------|-------|
| Resolution | 16-bit (signed, effective 15-bit positive) |
| Full-scale range | ±6.144 V (PGA_6144 setting) |
| Sampling | 5 kHz via hardware timer ISR |
| Channels | 4 per device (multiplexed) |
| Conversion mode | Single-shot, triggered by ISR |

### Sampling Strategy

The hardware timer ISR fires at 5 kHz and triggers the next ADC conversion. Results are read via I2C in the sampler task context (deferred from ISR). The effective per-channel sample rate is 5000 ÷ 4 = 1250 Hz.

### Calibration

Three-step calibration process:

#### 1. Zero Calibration

- Switch all inputs to SW_SHUNT mode (routes to GND via shunt resistor)
- Measure ADC reading — this is the zero offset
- Store per-channel offset values

#### 2. Gain Calibration

- Switch to SW_PULLUP mode (connects 5 V reference through known resistance)
- Measure ADC reading
- Calculate gain factor: `expected_voltage / measured_voltage`

#### 3. CMOS Switch Compensation

The analog CMOS switches have a non-zero on-resistance (~10 Ω) that introduces a measurable offset:

```
true_value_mV = 1.004528 × measured_value_mV + 0.0339
```

This linear compensation is applied after zero and gain correction.

### ADC Reading Chain

```
Sensor → CMOS switch → Conditioning → ADS1115 → I2C → ESP32
                                                       │
                                            Apply zero offset
                                            Apply gain factor
                                            Apply CMOS compensation
                                                       │
                                            Final value in mV
```

---

## DC-DC Converters

### Module

**RECOM ROE-0505S** (5 V isolated, unregulated)

- One per channel (A and B)
- Stacked for higher voltages (12 V = 2×, 24 V = 4× or stepped configuration)

### Characteristics

| Parameter | Value |
|-----------|-------|
| Isolation | 1 kV DC |
| Max current | 85 mA per channel |
| Regulation | Unregulated (output varies with load) |
| Efficiency | ~80% |

### Output Voltages

| Nominal Setting | Unloaded Output | Loaded (50 mA) |
|----------------|-----------------|-----------------|
| 5 V | ~6.0 V | ~5.4 V |
| 12 V | ~13.5 V | ~12.7 V |
| 24 V | ~26.5 V | ~25.1 V |

> **Design note:** The converters are intentionally unregulated. The ADC measures actual supply voltage and compensates. Sensor readings are ratiometric where possible.

### Voltage Selection

Channel voltage is selected via I2C expander at address 0x21. Control bits enable the appropriate converter combination for the requested voltage.

---

## Galvanic Isolation

Each channel is fully isolated from the ESP32 core and from the other channel:

```
┌─────────────────────────────────────────────────────┐
│  ESP32 Core (3.3 V domain)                          │
│  ├── I2C bus (3.3 V logic)                          │
│  ├── GPIO (digital inputs via optocouplers)         │
│  └── SPI (display, Ethernet)                        │
└────────────┬────────────────────────┬───────────────┘
             │ Isolated               │ Isolated
    ┌────────┴────────┐     ┌────────┴────────┐
    │  Channel A       │     │  Channel B       │
    │  ├── DC-DC (own) │     │  ├── DC-DC (own) │
    │  ├── Optocouplers│     │  ├── Optocouplers│
    │  ├── I2C expand. │     │  ├── I2C expand. │
    │  ├── ADC         │     │  ├── ADC         │
    │  └── 4 inputs    │     │  └── 4 inputs    │
    └─────────────────┘     └─────────────────┘
```

### Isolation Boundaries

- **DC-DC converters:** Provide isolated power (1 kV rated)
- **Optocouplers:** Isolate digital signals between channel and ESP32 GPIO
- **I2C isolation:** The ADCs and expanders on channel boards are powered from channel supply; I2C signals cross isolation via level-shifted optocouplers

---

## Relay

| Parameter | Value |
|-----------|-------|
| Part | TE Connectivity T9GV1L14-5 (5V coil) |
| Type | Electromechanical, SPST-NO |
| Coil voltage | 5 V DC |
| Coil current | ~70 mA |
| Contact rating | 15 A @ 250 V AC / 30 A @ 30 V DC |
| Control | Via I2C expander at 0x27 |

Two relays available: one per channel (AR and BR). Driven by the relay/serial board expander.

---

## Buzzer

| Parameter | Value |
|-----------|-------|
| Type | Piezo (passive) |
| Drive method | Hardware timer ISR (anti-phase via MAX3232 spare outputs) |
| Frequency range | 600–1600 Hz |
| GPIO | 21 |

### Drive Circuit

The buzzer is driven differentially using spare MAX3232 driver outputs in anti-phase. This doubles the effective voltage across the piezo element (from 3.3 V to ~6.6 V peak-to-peak), producing louder output without an external amplifier.

### Sound Definitions

| Sound ID | Pattern | Use |
|----------|---------|-----|
| 1 | Single short beep (100 ms @ 1 kHz) | Confirmation |
| 2 | Double beep | Transaction complete |
| 3 | Triple rapid beep | Warning |
| 4 | Ascending tone | Power on |
| 5 | Descending tone | Power off / shutdown |
| 6 | Long tone (1 s) | Error / alarm |

---

## Button Interface

Five physical buttons are connected to a single GPIO (pin 15) via a resistor ladder network. The ESP32's internal ADC reads the analog voltage, and the firmware determines which button is pressed based on voltage thresholds.

| Button | Approximate Voltage | Function |
|--------|-------------------|----------|
| None | 3.3 V (open) | No press |
| Up | 2.7 V | Navigate up |
| Down | 2.0 V | Navigate down |
| Left | 1.3 V | Back |
| Right | 0.7 V | Enter |
| Select | 0.0 V (GND) | Confirm/toggle |

Debounce: 50 ms in software.

---

## Known Rev 1 Issues

| # | Issue | Impact | Workaround | Rev 2 Fix |
|---|-------|--------|------------|-----------|
| 1 | No DC-DC activity LED | Cannot verify DC-DC output without multimeter | Measure with multimeter | Add LED per converter |
| 2 | UART TX needs external 10 kΩ pullup | RS-485 TX may float, corrupting bus | Add external pullup to VCC | Integrate on-board |
| 3 | ADC resistors ±1% tolerance | Limits system accuracy | Calibrate per-unit (zero + gain) | Upgrade to ±0.1% |
| 4 | No EMC filter on digital inputs | Susceptible to long-cable noise | Keep cable runs short, use shielded cable | Add input filtering |
| 5 | Buzzer loudness limited by 3.3 V drive | Adequate for enclosure, quiet in noisy environments | Anti-phase drive helps | Consider amplified driver |

---

## Related Documentation

- [Firmware Architecture](firmware-architecture.md) — how the firmware uses this hardware
- [Installation Guide](installation-guide.md) — wiring and mounting
- [Troubleshooting](troubleshooting.md) — diagnosing hardware faults
- [API Reference](api-reference.md) — commands for interacting with hardware
