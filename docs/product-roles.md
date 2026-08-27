# MCS Control — Produktroller

Control er én hardware-platform der sælges som navngivne produkter/roller.
Rollen bestemmer hvilke apps der kører, hvad der vises på skærmen, og hvordan config ser ud.

## CloudGauge 2

Probe-til-server gateway. Boksen er bindeled mellem probes og MCS Nexus server.

**Hardware:**
- 2 kanaler × 4 inputs = max 8 probes
- Kanal-voltage: 24V (standard for 4-20mA probes)

**Probe-typer:**
1. **Huba 712** (standard) — 4-20mA, 1 input per probe. Den vi sælger mest.
2. **Huba med temperatur** — 2 inputs per probe, sandsynligvis 0-5V. Ikke verificeret endnu.
3. **OLE-probe** — tilsluttes via en af serial-portene (RS-232/485). Detaljer ukendte.

**Logik:**
- App-drevet: sampling, averaging, konvertering (mA → cm → liter)
- Periodisk push til Nexus server
- function_silo: `cloudgauge_get_all`, `cloudgauge_get`

**Config-stil:** "Hvad er tilsluttet" — device-typer der udleder hardware-settings automatisk.

---

## Micro FMS

Komplet tankstations-controller med pumpelogik og transaktionshåndtering.

**Per kanal (× 2):**
- 1 relay → pumpe
- 1 digital input → nozzle (pullup, typisk 12V)
- 1 digital/pulse input → pulsgiver (mekanisk kontakt, kræver debounce)
- 1-2 inputs → valgfri probe (simpel 1-wire eller temp-probe 2-wire)

**Pulsgiver:**
- Mekanisk switch bouncer — skal have debounce-mode per input
- Ikke en ny ringbuffer, men filtreret readout med minimum-interval mellem edges

**Transaktionsflow:**
1. Modtag "start pump #" + transaktions-ID (via function_silo)
2. Vent på nozzle op
3. Start relay (pumpe)
4. Tæl pulser, send periodisk update (konfigurerbart interval)
5. Nozzle ned → stop relay → send endelig transaktion med total

**Kommunikation:**
- function_silo håndterer routing — pump_controller behøver ikke vide om data kommer fra BLE, WebSocket eller HTTP
- Kommandoer: `pump_start`, `pump_stop`, `pump_status`

**Config-stil:** "Hvad er tilsluttet" — device-typer der udleder hardware-settings automatisk.

---

## Poseidon

Site manager — ren I/O gateway uden applikationslogik.

**Koncept:**
- Real-time I/O adgang fra server-side
- Hvert input/output har et navn og en konfiguration
- Ingen app-logik — serveren bestemmer hvad der sker
- Multi-device: kan bruges til hvad som helst

**Input-modes:**
- **Output** — sæt HIGH/LOW fra server
- **Analog 0-5V** — sender analoge målinger retur (med konfigurerbart interval)
- **Analog 4-20mA** — strømmåling
- **Digital** — måler HIGH/LOW (med valgfri pullup og spænding)

**Per pin-konfiguration:**
- Navn (menneskelig beskrivelse)
- Retning (input/output)
- Mode (analog/digital/pulse)
- Voltage, pullup, shunt, report-interval

**Fysisk begrænsning:** Alle 4 inputs på en kanal deler voltage (5V/12V/24V).

**function_silo kommandoer:**
- `io_set` → sæt output HIGH/LOW
- `io_get` → læs enkelt input
- `io_get_all` → læs alt med navne og værdier

**Config-stil:** Eksplicitte hardware-settings per pin — ingen device-typer der udleder noget.

---

## Opsummering

| Rolle | Logik | Config-stil |
|---|---|---|
| CloudGauge 2 | App-drevet (probes, averaging, push) | Devices med typer → hardware udledt |
| Micro FMS | App-drevet (transaktioner, pumpelogik) | Devices med typer → hardware udledt |
| Poseidon | Ingen app-logik — ren I/O gateway | Eksplicit hardware-config per pin |
