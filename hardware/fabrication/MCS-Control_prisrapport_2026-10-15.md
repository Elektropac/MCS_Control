# MCS Control — Prisestimat & besparelsesmuligheder

Dato: 2026-10-15
Grundlag: BOM fra KiCad (BottomBoard, 264 komponenter) + moduler. Kun BottomBoard medregnet
(W5500/PoE er fremtidige tilføjelser og indgår ikke).

> **Vigtigt:** Alle priser er ESTIMATER (typiske distributørpriser, Mouser/Digikey-niveau).
> Nogle er bekræftet direkte (ROE-0512S, AQW212EHA, Waveshare-board, OLED).
> Beløb er KUN materiale pr. board — PCB-fremstilling, montage, stencil, test og certificering
> kommer oveni (det er dét Elprint prissætter). Alle priser i DKK pr. stk.

---

## 1. Nuværende estimat — pris pr. board

| Post | 100 stk | 500 stk |
|------|---------|---------|
| SMD-komponenter | ~770 DKK | ~593 DKK |
| ESP32-S3 ETH modul | ~152 DKK | ~152 DKK |
| OLED 2,42" 128×64 | ~90 DKK | ~90 DKK |
| **I alt pr. board** | **~1.010 DKK** | **~835 DKK** |

### Hvor pengene ligger (de store poster, pr. board @100 stk)
1. PhotoMOS i alt ~285 DKK (AQW212EHA ×18 ~215 + AQY211EHA ×6 ~67)
2. Moduler (ESP32 + OLED) ~242 DKK
3. DC-DC (RECOM ROE ×6) ~127 DKK
4. ADS1115 ADC ×2 ~57 DKK
5. De ~230 modstande/kondensatorer tilsammen <22 DKK — ubetydelige.

---

## 2. Besparelsesforslag — pr. board

### A. Indkøbskanal — LCSC/kinesisk (størst samlet effekt, "giver sig selv")
Flytter PhotoMOS, standard-IC'er og passive til 30–50 % lavere pris. Dækker især de to store
PhotoMOS-poster (~285 DKK), hvor et komponentskift IKKE hjælper (se punkt 3).
**Effekt: betydelig, lav risiko.**

### B. DC-DC: RECOM → MORNSUN (6 moduler pr. board)

| Valg | Pris pr. board | Besparelse pr. board |
|------|----------------|----------------------|
| RECOM (nu) | ~116 DKK | — |
| **MORNSUN (anbefalet)** | ~49 DKK | **~67 DKK** |
| Generisk kinesisk | ~25 DKK | ~92 DKK |

**Anbefaling: MORNSUN** — tager det meste af gevinsten uden no-name-risiko. Modulerne sidder i den
galvaniske isolationsbarriere (ground-loop-beskyttelse pumpe↔Control) — ikke stedet at gamble.
**Forbehold:** ROE er ureguleret (overspænding ved lav last) — bekræft at erstatning har
samme/bedre regulering + pin-kompatibel SIP-4 footprint.

### C. SAMTEC-header → billigere header
SAMTEC SMH-107 ~19 DKK for én. Generisk 2,54 mm header ~2–6 DKK.
**~15 DKK pr. board, lav risiko** (medmindre specifikt SAMTEC-krav).

### D. Mængde 100 → 500 stk
Giver alene ~23 % på komponenterne (770 → 593 DKK pr. board).

---

## 3. PhotoMOS — undersøgt, men IKKE et komponentskift (dokumentation)
De 18 PhotoMOS (16 til input-mode-switching: digital/analog + pullup/shunt, 2 til RS232/RS485-skift)
**kræver galvanisk isolation** (ground-loops field↔Control + to forskellige kanalspændinger ≤30 V) og
**bruger alle kontakter** (ægte 2 Form A).
- Analog-switch (ikke-isoleret) duer ikke — ville bryde isolationen.
- Skift til TLP222A-2 sparer intet hos Mouser/Digikey (samme pris som nu).
- TLP172AM (1 Form A) giver intet, da alle kontakter bruges.
- **→ Eneste PhotoMOS-besparelse er indkøbskanal (punkt A).**

---

## 4. Strategiske produktbeslutninger (største gevinst — kræver beslutning)

Produktvalg om brugergrænsefladen. Trappe-model, pris pr. board:

| Niveau | Beskrivelse | Besparelse pr. board |
|--------|-------------|----------------------|
| Fuld (nu) | Skærm + 5 knapper (TopBoard) | — |
| Drop knapper + TopBoard | Skærm på BottomBoard via kabel | ~55–97 DKK |
| Drop skærm oveni | "Blind" enhed, betjening via web/BLE/telefon | +~108 DKK |
| **Begge** | Ingen skærm, ingen knapper | **~160–205 DKK** |

**Afvejning:** Skal en montør i felten kunne se/betjene enheden lokalt uden telefon? Hvis al
konfiguration/aflæsning alligevel sker via web/BLE (Control-filosofien), er skærm+knapper luksus.
Kunne også være en **model-variant**: "med skærm" vs. billig "blind" udgave.
→ Beslutning hos Jesper/Simon.

---

## 5. Fremtidig / volumen-afhængig: integrer ESP32+W5500 selv (droppe købt board)

| | Pris pr. board |
|--|----------------|
| Køb Waveshare-board | ~152 DKK |
| Egne komponenter (ESP32-S3-WROOM + W5500 + RJ45 + USB + LDO + passive) | ~54 DKK |
| **Ren komponentbesparelse** | **~98 DKK pr. board** |

**MEN skjulte omkostninger:** designtid (W5500 impedans-kontrolleret Ethernet-layout, USB, RF),
dyrere/større PCB, mere montage (~36 ekstra dele), bringup + risiko. ESP32-S3-WROOM-modulet er
radio-præcertificeret (bevares), men boardets EMC skal valideres.
**Netto-besparelsen er mindre end 98 DKK** når montage + dyrere PCB trækkes fra.
**Break-even mellem 100 og 500 stk** — ikke værd ved 100, giver mening ved 500+.
Allerede på tegnebrættet (W5500-ark / Sheet_3, "til senere").

---

## Sammenfatning — prioriteret (besparelse pr. board)

| # | Tiltag | Besparelse pr. board | Risiko | Hvornår |
|---|--------|----------------------|--------|---------|
| A | LCSC-indkøb (PhotoMOS + IC + passive) | Stor del af ~285 DKK PhotoMOS | Lav | Nu |
| D | 100 → 500 stk | ~23 % på komponenter | — | Volumen |
| 4 | Drop skærm + knapper (produktvalg) | ~160–205 DKK | — (funktionsvalg) | Beslutning |
| B | DC-DC → MORNSUN | ~67 DKK | Lav-middel | Nu, m. verifikation |
| C | SAMTEC → billig header | ~15 DKK | Lav | Nu |
| 5 | Integrer ESP32+W5500 selv | ~98 DKK (netto mindre) | Middel-høj | Fremtid/volumen |

**Udgangspunkt:** ~835 DKK pr. board @500 stk.
**Hvis alt (realistisk) gennemføres** kan prisen komme ned omkring **~520–595 DKK pr. board @500 stk**,
afhængigt af produktbeslutningerne (skærm/knapper vægter tungest).
