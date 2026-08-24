# TBD: LDO-regulering på DC-DC output

## Problem

De uregulerede DC-DC isolatorer (RECOM ROE-0505S) leverer højere spænding end nominel ved lav last:

| Indstilling | Forventet | Målt (ubelastet) | Målt (~50mA) |
|-------------|-----------|-------------------|--------------|
| 5V          | 5.0V      | ~6.0V             | ~5.4V        |
| 12V         | 12.0V     | ~13.5V            | ~12.7V       |
| 24V         | 24.0V     | ~26.5V            | ~25.1V       |

## Hvem påvirkes?

| Enhed | Risiko | Kommentar |
|-------|--------|-----------|
| **5V probe (level+temp)** | HØJ | Probe spec'd til 5V, får 5.4-6V |
| 4-20mA probes (24V) | LAV | Typisk spec'd 10-36V DC, 26V er inden for |
| Pulsgivere (open-collector) | LAV | Vce(max) typisk 30-50V, strøm 5.2mA vs 4.8mA — ubetydelig |
| Optokoblere (digital input) | LAV | Virker fint ved 5-26V |

## Foreslåede løsninger

### Option A: LDO kun på 5V-linjen
- Én LDO (f.eks. AP2112K-5.0 eller MCP1802-5.0) bag 5V DC-DC
- Billig (~$0.10), minimal pladsbrug (SOT-23 + 2× cap)
- Løser det eneste reelle problem (5V-proben)
- Bidrager til min-load på DC-DC (quiescent + probe-forbrug)

### Option B: LDO på alle tre spændinger (5V, 12V, 24V)
- Præcis spænding på alle kanaler
- Fordel: ADC-kalibrering bliver lettere (stabil reference)
- Ulempe: Mere plads, mere BOM, og 24V LDO'er har højere dropout

### Option C: Ingen LDO, kun min-load LED (nuværende rev 2 plan)
- LED/modstand trækker ~10mA → dropper ubelastet output lidt
- Billigst og simplest
- Løser det IKKE for 5V-proben (6V → ~5.6V med LED, stadig for højt)

## Anbefaling

**Option A** — LDO kun på 5V-linjen. Det er den eneste spænding hvor overshoot er et reelt problem. 12V og 24V er inden for alle enheders tolerancer.

Min-load LED bør stadig tilføjes på alle tre linjer (stabiliserer DC-DC ved idle).

## Forbrug

| LDO-type | Quiescent | Dropout | Pris |
|----------|-----------|---------|------|
| AP2112K-5.0 | 55 µA | 250 mV @ 600mA | $0.15 |
| MCP1802-5002 | 1.6 µA | 450 mV @ 300mA | $0.25 |
| AMS1117-5.0 | 5 mA | 1.0V @ 1A | $0.08 |

Ved probe-load (~25mA): tab = (6V - 5V) × 25mA = **25 mW** (negligibelt).

## Beslutning

⬜ Afventer beslutning — diskutér med Simon/Flemming ved næste review.

## Relateret
- Rev 2 note: min. load LED/modstand på DC-DC
- Rev 2 note: 10kΩ pullup på UART TX
- Rev 2 note: 0.1% ADC modstande
