# MCS Control — Hardware (KiCad)

PCB- og schematic-filer for MCS Control, konverteret fra det oprindelige EasyEDA Pro-projekt til KiCad.

## Åbn projektet

Åbn `kicad/MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a.kicad_pro` i KiCad og start Schematic Editor.
Hold hele `kicad/`-mappen samlet — de lokale symbol-/footprint-biblioteker peges på via relative stier
(`sym-lib-table` / `fp-lib-table`).

## Boards

- **BottomBoard:** `kicad/MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a.kicad_pcb`
- **TopBoard:** `kicad/Sapphire_Topboard.kicad_pcb`

Schematic'et dækker begge print. Komponentfordelingen mellem de to boards skal håndteres **før**
"Update PCB from Schematic" bruges.

## Status efter konvertering (EasyEDA → KiCad, 28/9 2026)

Verificeret OK:
- 276 komponenter overført med referencer + værdier.
- Alle 973 forbundne ben matcher 1:1 mod EasyEDAs officielle Protel-netliste (forbindelser intakte).
- 274/276 komponenter har footprint. Lokale symbol- + footprint-biblioteker ligger i mappen.
- W5500- og PoE-delkontroller: ingen net-mismatch, ingen dublet-referencer.

Udestår før produktion:
- **U32 (W5500)** og **X2 (25 MHz krystal)** mangler footprints (fandtes ikke på kildeprintene).
- **ERC: 759 fund** — hovedsagelig importartefakter (uspecificeret elektrisk pin-type på importerede
  symboler), ikke nødvendigvis reelle fejl. Skal gennemgås, ikke skjules. Se `kicad/Importkontrol-ERC.rpt`.
- **PCB ikke valideret:** DRC, fremstillingsdata og schematic→PCB-sync er ikke kørt.
- **W5500/Ethernet-ark (Sheet_3)** er tegnet (52 komponenter, Waveshare-reference) men bevidst udeladt fra PCB.

## PoE

Aktuel PoE-forsyning: **PoE_5V** (5 V, op til ~2 A). Se `kicad/PoE-design.md`.
Den tidligere 24 V / 41 W-variant er arkiveret i `kicad/backup/PoE-41W-revA/`.
