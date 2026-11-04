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

**3D-visning rettet** (`hardware/tools/fix_3d_models.py`, kun model-felter):
- 18 PhotoMOS (SMD-8) manglede model på printet (kun sat i biblioteket) → tilføjet.
- 6 DC/DC (SIP-4 B2405S) pegede på en kopi af pin-header-modellen → KiCads Murata CRE1 (samme SIP-4-hus).
- EasyEDA placerer modeller efter midtpunkt, KiCad efter nulpunkt → klemrække U61, header J1, DC/DC drejet/flyttet
  så modellen dækker pads og courtyard.
- Relæ K1/K3 (EasyEDA-model ufuldstændig) og CN1/CN2 (model viste flerpolet klemme) → **forenklede kasser**
  i silketryk-omridsets mål (`3d/*.wrl`). Højde er skøn: relæ 20 mm, klemme 12 mm.
- **SMD-rotationer:** importen havde drejet mange modeller 90/180° (alle 144 R0603, 18 PhotoMOS, SOIC, SSOP,
  SMD-16, 6 SOT-23 på bunden). Rettet ved at finde den drejning hvor modellens benfødder (nederste
  STEP-hjørnepunkter) lander på pads — efterkontrol: 88–100 % for alle typer (SOP-4/SOP-8 kontrolleret visuelt).
- **U24 Waveshare ESP32-S3-ETH:** model tilføjet (`3d/waveshare_esp32s3eth.wrl`, fra control-emulator) — antager
  samme montering som pumpesimulatoren: ben loddet med 2,5 mm afstand, RJ45-siden op, USB ved printkanten.
- **LS1 buzzer (CPT-1667):** forenklet cylinder Ø16,6 mm, højde 8 mm (skøn).

**Tilbage — kræver manuel gennemgang:**
- 83 courtyard-overlap — **ACCEPTERET (beslutning 2026-11-04, Jesper):** courtyards er normale (~0,27 mm margin),
  komponenterne står reelt tæt: 0,30–0,5 mm pad-til-pad (ingen under 0,3 mm), kobber overholder 0,15 mm clearance.
  Printet er produceret og virker med denne placering → ingen flytning. Tætteste: D13/D14, D3/D4 (0,30 mm), R11/U20, R79/U44 (0,35 mm).
  Markeret som accepteret i `MCS-Control_…kicad_dru` (én regel pr. par, `severity ignore`). Nye overlap fanges stadig. DRC: **433 fund** (kun silk/kosmetik + 12 løse spor).
- 10× SOT-23 (2N7002) har courtyard *mindre* end egne pads (importfejl) — skjuler evt. overlap, giver ikke falske fund.
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
