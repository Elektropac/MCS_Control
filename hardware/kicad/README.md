## PoE forenklet 2026-09-28

Ét nyt ark: **PoE_5V**, 5 V med mål op til 2 A, tegnet med direkte ledninger. De tidligere 5/12/24 V-ark er arkiveret i backup/PoE-41W-revA. PCB er uændret. Læs [PoE-design.md](PoE-design.md) om integration og resterende hardwarevalidering.

## DRC-oprydning efter import (2026-11-04, branch `hw/drc-oprydning`)

`hardware/tools/fix_easyeda_import.py` rydder importrester på BottomBoard uden at ændre forbindelser
(alle 938 pads har samme net før/efter; 0 uforbundne). DRC: **1352 → 522 fund**.

- Clearance-regel 0,15 mm (layoutet er lavet med EasyEDA's 6 mil) og kobber-til-kant 0,3 mm.
- 106 bundkomponenter havde courtyard på forsidens lag → flyttet til B.Courtyard.
- 312 pads (alle passive R/C/SD0805) havde pasta-margin −100 mm = ingen stencil-åbning → nulstillet.
- 14 spor med længde 0 fjernet.
- Relæspor (5 mm) `B_RELAY_NO`/`B_RELAY_COM` lå 0,14/0,20 mm fra højre kant → lodrette stykker rykket 0,5 mm ind (nu ~0,65/0,70 mm). DRC: **516 fund**, 0 kobber-til-kant.

**Tilbage — kræver manuel gennemgang:**
- 83 courtyard-overlap, mest R/Q tæt på PhotoMOS (U). Tjek om det er reelt eller for store importerede courtyards.
- 12 spor med løs ende (ikke fjernet automatisk — flere spor har samme net/længde, så en automatisk match kan ramme forkert).
- ~420 silk-advarsler og 2 tekst-tykkelser (kosmetisk).

# MCS-Control i KiCad

**Seneste ændring:** Sheet_3 er nu erstattet af et nyt W5500/Ethernet-diagram med 52 komponenter efter Waveshares reference. Det er udeladt fra PCB. Se [W5500-Sheet_3.md](W5500-Sheet_3.md) for omfang, grænseflade og kontrol. De øvrige ark og begge PCB-filer er uændrede. Importstatus nedenfor beskriver versionen før denne ændring.

Åbn `MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a.kicad_pro`, og start Schematic Editor. Dobbeltklik på arkene i venstre side.

Projektet er importeret fra EasyEDA-backuppen den 28. september 2026. Alle syv kildeark er med samt et KiCad-oversigtsark. Sheet_2 var tomt i originalen; IO Expander indeholder en referencetabel som billede.

## Gennemført

- 276 komponenter overført med referencer og værdier.
- Lokale symbolbiblioteker og footprintbibliotek ligger i projektmappen.
- Siderammer og placering er tilpasset KiCad.
- Alle 973 tilsluttede ben i EasyEDAs officielle Protel-netliste har præcis samme forbindelsesgrupper i KiCad. KiCad viser desuden 66 isolerede ben.
- 274 komponenter har fået footprinttilknytning: 264 fra de oprindelige printlayouts og 10 modstande/kondensatorer genbruger samme kildes package-type. Padnumrene for de 264 direkte matches er kontrolleret mod symbolerne.
- BottomBoard er filen med projektets lange navn og endelsen `.kicad_pcb`. TopBoard er `Sapphire_Topboard.kicad_pcb`.

## Udestår før produktion eller automatisk PCB-opdatering

U32 (W5500) og X2 (25 MHz krystal) mangler footprints; de findes ikke på de to kildeprint. Der er ikke valgt erstatninger uden package-verifikation.

ERC giver 759 fund: 704 pin-type-konflikter, 29 uforbundne ben, 8 forsyningsben uden driver, 6 isolerede pin-labels, 6 ben uden driver, 4 netnavnekonflikter og 2 løse labels. Mange importerede symbolben har uspecificeret elektrisk type. Fundene er bevaret til elektrisk gennemgang; de er ikke skjult ved vilkårlige ændringer af pin-typer. Se `Importkontrol-ERC.rpt`.

Netlistematch dokumenterer bevarede forbindelser, ikke at originalkredsløbet er elektrisk korrekt. De to PCB-filer er importerede layouts; DRC, fremstillingsdata og diagram-til-PCB-synkronisering er ikke valideret. Diagrammet omfatter begge print, og komponentfordelingen skal håndteres inden brug af Update PCB from Schematic.

Behold hele mappen samlet, så de relative biblioteksstier virker.
