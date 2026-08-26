# MCS Control — Installation Guide

This guide covers physical installation, wiring, and initial power-on of the MCS Control unit. It is intended for technicians and distributors performing field installations.

---

## Mounting

### Dimensions

- **Board size:** 104 × 104 mm
- **Mounting:** DIN-rail clip or panel mount (4× M3 corner holes)

### Environment

- Install in a weather-protected enclosure (IP54 minimum recommended)
- Ensure adequate ventilation — no forced cooling required at normal loads
- Keep away from heat sources and direct sunlight
- Operating temperature: 0–50 °C (ambient)

### Orientation

- Mount with connectors facing down (allows cable drip loops)
- OLED display should face the operator if local readout is needed

---

## Power Supply

| Parameter | Specification |
|-----------|--------------|
| Input voltage | 12–24 V DC |
| Typical consumption | < 500 mA at 24 V (all channels active) |
| Connector | Screw terminal (2-pin) |
| Protection | Reverse polarity protection on-board |

### Requirements

- Use a regulated DC power supply rated for at least 1 A
- Keep power cable runs short (< 5 m) or use adequate gauge (≥ 0.75 mm²)
- If powering from a vehicle battery, add a DC-DC converter for clean supply

---

## Channel Wiring

MCS Control has two independent, galvanically isolated channels: **A** and **B**. Each channel provides its own power supply (selectable 5V/12V/24V) and four configurable inputs.

### Important: Isolation

- Channel A, Channel B, and the ESP32 core are **galvanically isolated** from each other
- Each channel has its own DC-DC converter and optocouplers
- Do **not** bridge grounds between channels — this defeats the isolation
- Channel ground is the reference for that channel's inputs only

### Pump Pulse Counter (Flow Meter)

Typical connection for a pulse-output flow meter:

```
Channel terminal:
  Input Ax (or Bx) ──── Pulse output from meter
  GND (channel)    ──── Meter ground

Configuration: mode = "pulse", pullup = true
```

- Set input mode to **pullup + digital** for open-collector pulse outputs
- The internal pullup is referenced to the channel voltage (5V/12V/24V)
- Maximum pulse frequency: 2.5 kHz (sampled at 5 kHz)
- Configure `pulses_per_liter` in the pump function definition

### Nozzle Switch (Pump Handle)

```
Channel terminal:
  Input Ax (or Bx) ──── Nozzle switch (N.O. contact)
  GND (channel)    ──── Switch common

Configuration: mode = "digital", pullup = true
```

- Switch closes when nozzle is lifted (pump ready)
- Internal pullup ensures clean high state when switch is open

### 4–20 mA Probe (Tank Level)

```
Channel terminal:
  Input Ax (or Bx) ──── Probe signal (+)
  GND (channel)    ──── Probe signal (−) / loop return

Configuration: mode = "analog_current"
```

- The unit provides a precision shunt resistor for current-to-voltage conversion
- Configure linear conversion parameters for your probe's range
- Supports 2-wire (loop-powered) probes when channel voltage matches probe requirements

### Wiring Summary Table

| Input Role | Mode | Pullup | Typical Sensor |
|-----------|------|--------|----------------|
| Flow meter pulse | pulse | Yes | Open-collector pulse output |
| Nozzle switch | digital | Yes | N.O. dry contact |
| Tank probe (4–20 mA) | analog_current | No | 2-wire level transmitter |
| Voltage sensor (0–5 V) | analog_voltage | No | Pressure/temp transducer |

---

## Relay Wiring

Each channel has one relay output (AR for channel A, BR for channel B).

| Parameter | Specification |
|-----------|--------------|
| Contact type | Dry contact (NO + COM) |
| Maximum voltage | 30 V DC / 250 V AC |
| Maximum current | 5 A resistive |
| Coil voltage | 5 V (driven via I2C expander) |

### Typical Pump Control Wiring

```
Relay AR:
  COM ──── Pump contactor coil (+)
  NO  ──── Supply voltage for contactor

When relay energises: pump contactor closes → pump runs
When relay de-energises: pump stops (fail-safe)
```

> **Warning:** For loads exceeding relay ratings, use the relay to drive an external contactor.

---

## Communication Connections

### Ethernet (RJ45)

- Standard RJ45 connector
- 10/100 Mbps (W5500 controller)
- Supports DHCP or static IP configuration
- Use shielded Cat5e or better cable
- Maximum cable run: 100 m

### RS-485

| Terminal | Function |
|----------|----------|
| A (+) | Non-inverting (D+) |
| B (−) | Inverting (D−) |
| GND | Signal ground (reference) |

- Half-duplex, up to 32 devices on bus
- 120 Ω termination resistor switchable via software
- Maximum cable length: 1200 m at 9600 baud
- Use twisted pair cable (shielded recommended)
- **Rev 1 note:** External 10 kΩ pullup required on TX line (see [Troubleshooting](troubleshooting.md))

### RS-232

| Terminal | Function |
|----------|----------|
| TX | Transmit (from MCS Control) |
| RX | Receive (to MCS Control) |
| GND | Signal ground |

- Standard RS-232 voltage levels (±12 V via MAX3232 charge pump)
- Point-to-point connection only
- Maximum cable length: 15 m
- Commonly used for MCS Console link

---

## Grounding and Isolation Notes

1. **Do not connect channel grounds together** — channels A and B are fully isolated from each other
2. **Do not connect channel grounds to the ESP32/power supply ground** — isolation is maintained through DC-DC converters and optocouplers
3. **Ethernet ground is connected to ESP32 ground** — this is the system's earth reference
4. **RS-485 GND should be connected** between devices on the bus to provide a common-mode reference
5. **Shielded cable shields** should be grounded at one end only (preferably at the MCS Control enclosure) to prevent ground loops

---

## First Power-On Checklist

Before applying power, verify:

- [ ] Power supply voltage is within 12–24 V DC range
- [ ] Power polarity is correct
- [ ] No short circuits on channel outputs
- [ ] Ethernet cable connected (if using wired network)
- [ ] RS-485 bus terminated correctly (if applicable)
- [ ] Relay loads do not exceed ratings
- [ ] Enclosure is properly closed and sealed

### Power-On Sequence

1. Apply power
2. **Buzzer:** Single short beep = power-on acknowledged
3. **OLED display:** Splash screen with firmware version (within 2 seconds)
4. **Self-test:** Runs automatically on first boot
5. **Network:** Ethernet link LED on RJ45 illuminates (if cable connected)
6. **Ready:** Display shows main menu / dashboard

### Startup Indicators

| Indicator | Meaning |
|-----------|---------|
| Single beep + display on | Normal startup |
| Three rapid beeps | Self-test found warnings (non-critical) |
| Continuous beep (5 s) | Critical self-test failure — check hardware |
| No display, no beep | No power or power supply fault |
| Display on, no network LED | Ethernet cable or switch issue |

---

## Next Steps

- [Configuration](configuration.md) — configure channels, pumps, and network
- [Troubleshooting](troubleshooting.md) — if startup does not complete normally
- [Hardware Reference](hardware-reference.md) — detailed pin and device information
