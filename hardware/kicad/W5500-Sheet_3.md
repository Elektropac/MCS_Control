# Sheet_3 - W5500 Ethernet

Sheet_3 er tegnet fra bunden den 28. september 2026 efter W5500-delen af [Waveshares ESP32-S3-ETH-diagram, dateret 2024-12-21](https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf). ESP32-S3, regulator, USB, flash, kamera og SD-kort er ikke kopieret.

Arket indeholder 52 komponenter: W5500, 25 MHz krystal, seks 20 ohm seriemodstande til MCU, bias og afkobling, ferritfilter, to status-LED'er, H1102NLT-transformer, RJ45, kabelterminering og skærmkondensatorer. Det passive PoE-udtag fra Ethernet-parrene er med som J201; der er intet PoE-forsyningskredsløb.

Alle komponenter på arket er markeret **Exclude from board**. Der er ikke udført Update PCB from Schematic. Begge PCB-filer og alle øvrige diagramark er verificeret byte for byte som uændrede.

## Grænseflade til den senere integration

Netnavnene på Sheet_3 er lokale. De forbinder ikke automatisk det nye kredsløb til det eksisterende modul U24 eller de øvrige ark.

| Net på MCU-siden | Funktion | W5500-ben | Seriemodstand |
|---|---|---|---|
| GPIO9 | RSTn | 37 | R200, 20 ohm |
| GPIO10 | INTn | 36 | R201, 20 ohm |
| GPIO11 | MOSI | 35 | R202, 20 ohm |
| GPIO12 | MISO | 34 | R203, 20 ohm |
| GPIO13 | SCLK | 33 | R204, 20 ohm |
| GPIO14 | SCSn | 32 | R205, 20 ohm |
| ETH_3V3_IN | Ekstern 3,3 V indgang | Via L200 til AVDD og VDD | - |
| ETH_GND | Kredsløbets stel | AGND og GND | - |

ETH_SHIELD er et separat skærmnet. Det er koblet kapacitivt til ETH_GND som i Waveshares diagram. PMODE-benene er åbne med interne pull-ups som i referencekredsløbet. W5500's INTn-pin er defineret som output i det lokale symbol efter [WIZnets datablad](https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf).

## Kildetro detaljer og senere komponentvalg

- LED-polariteten følger Waveshare: anode mod IC via 600 ohm og katode til stel. W5500's LINK/ACT-signaler er aktive lave, så lysvisningen er inverteret. Dette er også skrevet på arket.
- Alle værdier, herunder 12,4 kohm/1 %, 18 pF, 6,8 nF, 22 uF og kabelsidens terminering, er overført fra referencen.
- Komponentfeltet `Waveshare_Ref` og `W5500-komponenter.csv` viser mapping til originalens referencebetegnelser. Nummereringen i MCS-Control er unik.
- Kun U32 har fået et standardfootprint (LQFP-48, 7 x 7 mm, 0,5 mm pitch). Endelige footprints og konkrete varianter af krystal, ferrit, RJ45, transformer og passiver skal vælges ved PCB-integrationen. Der er ikke gættet på disse ud fra PDF'en.

## Kontrol

Alle 176 fysiske ben er kontrolleret mod de forventede netgrupper, inklusive bevidst uforbundne ben. Ingen afvigelser eller dobbelte komponentreferencer blev fundet. Netlisten er eksporteret af KiCad fra en midlertidig kontrolkopi, hvor komponenterne var inkluderet; arbejdsversionen er fortsat udeladt fra PCB.

KiCad ERC i kontrolkopien giver ingen fund på Sheet_3. De øvrige arks eksisterende ERC-fund er ikke behandlet her. Netkontrollen og ERC er ikke en validering af et fremtidigt fysisk print eller en funktionstest af kredsløbet.

Den gamle tegning er bevaret i `backup/Sheet_3-before-W5500.kicad_sch`. Det nye lokale symbolbibliotek hedder `MCS_W5500.kicad_sym`.
