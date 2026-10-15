# MCS Control — Fabrikationspakke til Elprint

Dato: 2026-10-15

## Vigtigt — læs først

Projektet er flyttet fra EasyEDA til **KiCad**. Denne pakke er genereret direkte fra
KiCad-projektet, så **BOM og Pick & Place matcher nu 100%** (samme designatorer, samme kilde).

De tidligere uoverensstemmelser (komponenter der var i BOM men ikke i PnP og omvendt)
skyldtes at de to filer kom fra to forskellige eksport-versioner. **Brug venligst KUN
filerne i denne pakke — kassér den gamle BOM/PnP**, da reference-numrene er ændret
(fx er konnektorerne nu CN1/CN2 + U61, decoderen er U57 osv.).

## Hvad skal bygges

- **BottomBoard** = hovedprintet. Dette er det der skal prissættes/bygges.
- **TopBoard** (Sapphire Topboard) er med i pakken, men **er ikke helt færdigt endnu** —
  det består pt. kun af 5 knapper + 6 modstande + 1 header. Kig primært på BottomBoard.

## Komponenter der IKKE skal monteres / ikke er med

- **ESP32 (U24): monteres IKKE.** Modulet sættes i separat bagefter.
- **W5500 / Ethernet (Sheet_3) og PoE** er fremtidige tilføjelser og er **ikke** på printet
  endnu — derfor er de ikke med i denne BOM.

## Filer i pakken

| Fil | Indhold |
|-----|---------|
| `MCS-Control_BOM_BottomBoard.csv` | BOM, bund-board — 252 komponenter |
| `MCS-Control_BOM_TopBoard.csv` | BOM, top-board — 12 komponenter |
| `MCS-Control_BottomBoard-pos_clean.csv` | Pick & Place, bund-board |
| `MCS-Control_Sapphire_Topboard-pos_clean.csv` | Pick & Place, top-board |

Pick & Place-format: `Ref, Val, Package, PosX, PosY, Rot, Side` (CSV, enheder i mm).
Rotationer er verificeret mod den oprindelige EasyEDA-fil (som den fungerende prototype blev
bygget efter) — de er identiske.

## Hele KiCad-projektet

Hele KiCad-projektet er **vedlagt i pakken** under `kicad-projekt/`, så I kan slå alt op
direkte (schematic, PCB, symbol- og footprint-biblioteker). Åbn
`kicad-projekt/MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a.kicad_pro` i KiCad 10.
Hold mappen samlet (biblioteker peges på via relative stier). Gerber kan sendes på forespørgsel.

## Spørgsmål besvaret

- *"Diotec valgt i stedet for 1N4148W (ChipNobo)"* — fint, ingen indvending.
- *"P1/P2/P3, R4/R5, R6/R24-R28, SW1-5, U13, U30 var ikke i PnP"* — løst: alle findes nu
  i PnP under korrekte designatorer (nogle var top-board, konnektorerne hed noget andet).
- *"C11-C58, CN1, CN2, J1, LS1, R29/R30, U24, U57, U59, U61 var i PnP men ikke i BOM"* —
  løst: de er nu i BOM. (U24/ESP32 = monteres ikke, se ovenfor.)
