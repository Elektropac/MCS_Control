# MCS Control — Config Specification

## Fælles struktur (alle roller)

```json
{
  "product": "cloudgauge2",
  "version": 1,
  "name": "Tank Farm North",
  "channels": {
    "A": { "voltage": 24 },
    "B": { "voltage": 24 }
  },
  "connection": {
    "server": {
      "local": { "host": "192.168.10.129", "port": 3000, "path": "/ws" },
      "global": { "host": "nexus.mcscardsystems.com", "port": 443, "path": "/ws" }
    },
    "internet_client": "wifi",
    "settings": {
      "ethernet": { "dhcp": true },
      "wifi": { "ssid": "...", "password": "..." }
    }
  }
}
```

### Felter

| Felt | Type | Beskrivelse |
|---|---|---|
| `product` | string | Rolle: `cloudgauge2`, `micro_fms`, `poseidon` |
| `version` | int | Config-version (til migration) |
| `name` | string | Brugervenligt navn til display/server |
| `channels` | object | Kanal-voltage (altid påkrævet, hardware-niveau) |
| `connection` | object | Server + netværk (Niklas' domæne) |

---

## MCS Probe-typer

| MCS Type | Beskrivelse | Inputs | Voltage | Mode |
|---|---|---|---|---|
| `mcs_level` | Standard niveaumåler (4-20mA) | 1 | 24V | analog_current + shunt |
| `mcs_level_temp` | Niveaumåler med temperatur (0-5V, 2-wire) | 2 | 5V | analog_voltage (TBD) |
| `mcs_serial_probe` | Serial probe via RS-232/485 | 0 (serial port) | N/A | serial |

Koden mapper type → hardware-krav. Ved opstart valideres at kanal-voltage matcher probe-type.

---

## CloudGauge 2

```json
{
  "product": "cloudgauge2",
  "version": 1,
  "name": "Tank Farm North",
  "channels": {
    "A": { "voltage": 24 },
    "B": { "voltage": 24 }
  },
  "connection": { ... },
  "probes": [
    { "id": "tank_1", "type": "mcs_level", "input": "A1" },
    { "id": "tank_2", "type": "mcs_level", "input": "A2" },
    { "id": "tank_3", "type": "mcs_level", "input": "A3" },
    { "id": "tank_4", "type": "mcs_level", "input": "A4" },
    { "id": "tank_5", "type": "mcs_level", "input": "B1" },
    { "id": "tank_6", "type": "mcs_level", "input": "B2" },
    { "id": "tank_7", "type": "mcs_level", "input": "B3" },
    { "id": "tank_8", "type": "mcs_level", "input": "B4" }
  ]
}
```

### Apps der startes: `cloudgauge`
### Silo-kommandoer: `cloudgauge_get_all`, `cloudgauge_get`

---

## Micro FMS

```json
{
  "product": "micro_fms",
  "version": 1,
  "name": "Gas Station East",
  "channels": {
    "A": { "voltage": 12 },
    "B": { "voltage": 12 }
  },
  "connection": { ... },
  "pumps": [
    {
      "id": "pump_1",
      "nozzle": "A1",
      "pulser": "A2",
      "relay": "A",
      "debounce_ms": 20,
      "update_interval_s": 5,
      "pulses_per_liter": 100
    },
    {
      "id": "pump_2",
      "nozzle": "B1",
      "pulser": "B2",
      "relay": "B",
      "debounce_ms": 20,
      "update_interval_s": 5,
      "pulses_per_liter": 100
    }
  ],
  "probes": [
    { "id": "tank_1", "type": "mcs_level", "input": "A3" }
  ]
}
```

### Apps der startes: `pump_controller`, `cloudgauge`
### Silo-kommandoer: `pump_start`, `pump_stop`, `pump_status`, `cloudgauge_get_all`, `cloudgauge_get`

### Pump-felter

| Felt | Type | Beskrivelse |
|---|---|---|
| `id` | string | Pumpe-identifikator |
| `nozzle` | string | Input til nozzle-switch (digital, pullup) |
| `pulser` | string | Input til pulsgiver (digital, debounced) |
| `relay` | string | Relay-kanal: `A` eller `B` |
| `debounce_ms` | int | Minimum interval mellem edges (mekanisk switch) |
| `update_interval_s` | int | Hvor ofte der sendes løbende update under tankning |
| `pulses_per_liter` | float | Konverteringsfaktor |

---

## Poseidon

```json
{
  "product": "poseidon",
  "version": 1,
  "name": "Site Controller Dock 7",
  "channels": {
    "A": { "voltage": 24 },
    "B": { "voltage": 12 }
  },
  "connection": { ... },
  "io": [
    { "pin": "A1", "name": "Valve Control",  "mode": "output" },
    { "pin": "A2", "name": "Flow Sensor",    "mode": "analog_current", "interval_s": 10 },
    { "pin": "A3", "name": "Door Switch",    "mode": "digital", "pullup": true },
    { "pin": "A4", "name": "Tank Level",     "mode": "analog_current", "interval_s": 30 },
    { "pin": "B1", "name": "Overfill Alarm", "mode": "digital", "pullup": true },
    { "pin": "B2", "name": "Pump Enable",    "mode": "output" },
    { "pin": "B3", "name": "Temp Sensor",    "mode": "analog_voltage", "interval_s": 60 },
    { "pin": "B4", "name": "Unused",         "mode": "disabled" }
  ]
}
```

### Apps der startes: `poseidon_io`
### Silo-kommandoer: `io_set`, `io_get`, `io_get_all`

### IO-modes

| Mode | Beskrivelse | Ekstra felter |
|---|---|---|
| `output` | Sæt HIGH/LOW fra server | — |
| `digital` | Læs HIGH/LOW | `pullup` (bool) |
| `analog_current` | 4-20mA måling via shunt | `interval_s` |
| `analog_voltage` | 0-5V direkte måling | `interval_s` |
| `pulse` | Tæl pulser | `debounce_ms`, `interval_s` |
| `disabled` | Ikke i brug | — |

---

## Validering ved opstart

Koden skal ved boot:
1. Læs `product` → start de rigtige apps
2. Læs `channels` → sæt voltage
3. For CloudGauge/Micro FMS: valider at probe-typer matcher kanal-voltage
4. For Poseidon: konfigurer hvert pin direkte fra `io`-listen
5. Fejl → log + OLED-besked, men crash ikke
