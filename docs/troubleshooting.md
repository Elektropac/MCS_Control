# MCS Control — Troubleshooting

This guide is for support technicians and field engineers diagnosing issues with MCS Control units.

---

## Startup Issues

### No Display, No Beep

- **No power:** Verify 12–24 V DC at the power terminals with a multimeter
- **Reverse polarity:** Check terminal markings (protection diode will prevent damage but unit won't start)
- **Blown fuse:** Check inline fuse on power supply
- **Dead unit:** If voltage is present at terminals but nothing happens, the board may have a hardware fault — return for service

### Display On, No Network

- **Ethernet:** Check RJ45 link LED (should be solid or blinking). Try a different cable or switch port
- **WiFi:** Verify SSID/password in config. Use serial command `d` to check WiFi status
- **IP conflict:** Check for duplicate IPs on the network. Try DHCP if using static

### Buzzer Patterns at Startup

| Pattern | Meaning | Action |
|---------|---------|--------|
| 1 short beep | Normal boot | None required |
| 3 rapid beeps | Non-critical self-test warnings | Check display for details, usually still operational |
| 5-second continuous | Critical hardware failure | Run self-test, check I2C bus, check power supply |
| Repeating 2-beep pattern | Boot loop | Firmware may be corrupt — reflash via serial |

---

## Self-Test

The 88-point automated self-test verifies all hardware subsystems. Run it via:

- **Serial:** Send command `w` (USB, 115200 baud)
- **Web UI:** Navigate to `/test.html`
- **API:** POST `{"subject": "selftest", "data": {"section": "all", "verbose": true}}`

### I2C Failures

**Symptoms:** "Device not found at 0xXX" or "I2C timeout"

| Address | Device | Check |
|---------|--------|-------|
| 0x21 | Voltage/version board | Ribbon cable, I2C pull-ups |
| 0x23 | Input expander B | Channel B board connection |
| 0x25 | Input expander A | Channel A board connection |
| 0x27 | Serial/relay board | Relay board ribbon cable |
| 0x48 | ADC channel A | Channel A board connection |
| 0x49 | ADC channel B | Channel B board connection |

**Fixes:**
- Check ribbon cable connections (reseat firmly)
- Verify I2C pull-up resistors (4.7 kΩ to 3.3 V on SCL/SDA)
- Check for bus contention (only one master allowed)
- Verify 3.3 V supply to I2C devices

### Analog Failures

**Symptoms:** ADC reads wrong value, stuck at 0 or full-scale

- **Wrong voltage selected:** The ADC reading depends on the channel voltage setting. Verify `channel_voltage` matches actual DC-DC output
- **Input wiring:** Open circuit reads as noise; short to ground reads zero
- **Shunt resistor (current mode):** Verify 249 Ω precision resistor is intact
- **CMOS switch state:** Verify correct mode is set via `io_read` command

### Digital Input Failures

**Symptoms:** Input appears inverted or stuck

> **Important:** Digital inputs use optocouplers and are **inverted** — a closed switch (current flowing through opto-LED) reads as LOW internally. The firmware compensates for this, but when probing raw signals, expect inversion.

- **No signal:** Check that channel voltage is correct and sensor is powered
- **Stuck high/low:** Verify optocoupler LED current (1–20 mA typical)
- **Intermittent:** Check for marginal drive current — increase channel voltage if sensor allows

### Supply Failures

**Symptoms:** Channel voltage too low or unstable

- **Check DC-DC output** with a multimeter on the channel terminals
- **Minimum load:** The RECOM ROE-0505S converters are unregulated and require a minimum load for stable output. Unloaded voltages will be 10–15% higher than nominal
- **Overload:** Maximum 85 mA per channel. Calculate total sensor current draw
- **Rev 1 limitation:** No LED indicator for DC-DC status — must measure with multimeter

Expected voltages:

| Setting | Unloaded | Under Load (50 mA) |
|---------|----------|---------------------|
| 5 V | ~6.0 V | ~5.4 V |
| 12 V | ~13.5 V | ~12.7 V |
| 24 V | ~26.5 V | ~25.1 V |

---

## Network Issues

### Ethernet

- **No link LED:** Cable fault, switch port issue, or W5500 not initialised
- **Has link but no IP:** DHCP server unreachable. Check that DHCP is enabled on the network
- **Can ping but web UI won't load:** Port conflict or web server task crashed. Reboot the unit

### WiFi

- Use serial command `d` to see WiFi scan results and connection status
- **Won't connect:** SSID is case-sensitive. Check for hidden networks. Verify password
- **Keeps disconnecting:** Interference, weak signal, or AP channel hopping. Move closer or use Ethernet

---

## ADC Reading Wrong

### Recalibration Procedure

1. **Zero calibration:** Set input to shunt mode (routes to GND), run serial command `c`. This measures and stores the zero offset
2. **Gain calibration:** Apply a known 5 V reference to the pullup input. The firmware calculates the gain correction factor
3. **Verify:** Switch to the desired mode and compare reading against a reference meter

### CMOS Switch Compensation

The analog CMOS switches introduce a small linear error:

```
true_value = 1.004528 × measured_value + 33.9 µV
```

This compensation is applied automatically in firmware. If readings are still off:

- Check that calibration was performed after the last firmware update
- Verify precision resistor tolerance (should be ±0.1%, rev 1 uses ±1% — see Known Limitations)

---

## Relay Not Clicking

1. **Check I2C bus:** Relay is controlled via expander at address 0x27. Run `i` command to verify device responds
2. **Verify coil voltage:** The relay requires 5 V from the system supply (not channel supply)
3. **Test via command:** Use serial command `x` (relay A) or `y` (relay B) to toggle
4. **Listen for click:** If relay clicks but load doesn't respond, check the NO/COM wiring
5. **I2C expander fault:** If 0x27 doesn't respond, check ribbon cable to relay board

---

## UART Not Working

### RS-485 Issues

- **Rev 1 hardware issue:** The TX line requires an external **10 kΩ pullup** resistor to the RS-485 driver's VCC. Without it, the driver output may float and corrupt data
- **Termination:** Enable 120 Ω termination only at the two ends of the bus (not intermediate nodes)
- **Biasing:** Ensure A/B bias resistors are present if no devices are actively driving the bus
- **Direction control:** The firmware handles TX/RX switching automatically via RTS. If data is garbled, check for timing issues at high baud rates

### RS-232 Issues

- **Check charge pump capacitors:** The MAX3232 requires 4× 100 nF capacitors for voltage doubling. Missing or failed caps cause weak output levels
- **Null modem:** If connecting two DTE devices, cross TX/RX
- **Voltage levels:** RS-232 expects ±3 V to ±12 V. Logic-level (3.3 V) serial will not work — use the RS-232 mode, not direct GPIO

### Diagnostic Commands

```
u         → UART help menu
ua232     → Set channel A to RS-232
ua485     → Set channel A to RS-485
usa Hello → Send "Hello" on channel A
ul        → Listen on both channels for 5 seconds
uloop     → Loopback test (requires A↔B crossover cable)
```

---

## Firmware Update Failed

### OTA Recovery

The unit has dual OTA partitions. If an update fails mid-write:

1. The unit will boot from the previous (known-good) partition
2. Retry the OTA update
3. If both partitions are corrupt, use serial flash

### Serial Flash Procedure

1. Connect USB cable to the ESP32-S3 USB port
2. Install [PlatformIO](https://platformio.org/) or [esptool.py](https://github.com/espressif/esptool)
3. Put device in download mode: hold BOOT button while pressing RESET
4. Flash:
   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x0 firmware.bin
   ```
5. Reset the device (press RESET or power cycle)
6. Upload filesystem:
   ```bash
   pio run --target uploadfs
   ```

---

## Known Limitations (Rev 1)

| Issue | Impact | Workaround |
|-------|--------|------------|
| No min-load LED on DC-DC converters | Cannot visually verify DC-DC is running | Measure output with multimeter |
| UART TX requires external 10 kΩ pullup | RS-485 may not transmit without pullup | Add external pullup resistor |
| ADC precision resistors ±1% tolerance | Limits measurement accuracy to ~±1% | Use zero/gain calibration to compensate |
| Unregulated DC-DC output | Voltage varies with load (±15% unloaded) | Normal — design accounts for this |
| CMOS switch resistance (~10 Ω) | Introduces small measurement error | Firmware applies linear compensation |

### Planned Rev 2 Fixes

- Upgrade ADC resistors to ±0.1% tolerance
- Add DC-DC activity LEDs
- Integrate UART TX pullup on-board
- Add regulated DC-DC option for precision applications
- Improve EMC filtering on digital inputs

---

## Getting More Help

- **Serial debug dump:** Command `d` outputs full system state
- **Memory info:** Command `m` shows free SRAM and PSRAM
- **Task list:** Command `t` shows FreeRTOS task states and stack usage
- **Runtime stats:** Command `r` shows CPU time per task
- **Web test UI:** `/test.html` provides interactive hardware testing

For issues not covered here, contact the MCS engineering team with the output of the serial `d` command and the self-test results (`w` command).
