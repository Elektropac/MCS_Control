# PoE-forsyning — prototype A

Krav: 5 V / 1 A, 12 V / 1 A og 24 V / 1 A samtidigt, i alt 41 W. Alle tre udgange har fælles isoleret stel, POE_GND. De er ikke isoleret indbyrdes.

Der er tilføjet tre redigerbare KiCad-ark med 95 komponenter:

- **PoE_Input**: fire PoE-strømudtag, LT4321 og otte aktive bro-MOSFET'er samt transientbeskyttelse.
- **PoE_24V**: LT4295, klasse 7, isoleret synkron flyback med 24 V som fælles bus. Udgangspunkt ADI DC2476A-A. Separate komponenter, ikke et færdigt PoE-modul.
- **PoE_Outputs**: to LMR33620ADDA-regulatorer, 400 kHz, til 5 V og 12 V samt udgangsstik og isoleret T2P-status.

Alle nye komponenter er udeladt fra PCB. De oprindelige diagramark og begge PCB-filer er uændrede. Ingen nye udgange er forbundet med de eksisterende forsyningsnet. J300 er et foreløbigt diagraminterface til de fire kabelside-centerudtag, ikke en færdig RJ45-indgang.

## Dimensionering

| Udgang | Samtidig belastning | Nyttig effekt |
|---|---:|---:|
| 5 V | 1 A | 5 W |
| 12 V | 1 A | 12 W |
| 24 V | 1 A | 24 W |

Med antaget 90 % virkningsgrad i hver buck-regulator kræver 24 V-bussen 24 + 12/0,90 + 5/0,90 = 42,89 W, dvs. 1,79 A. ADI angiver op til 24 V / 2,4 A for referencekredsløbet ved klasse 7. Med yderligere tab i flyback og indgang er ca. 48–50 W et foreløbigt budget, ikke en måling. Designmålet er en IEEE 802.3bt Type 4, klasse 7 strømforsyning med 62 W til rådighed ved PD. En switch/injector skal kunne tildele klasse 7-effekt på den anvendte port.

R319 = 64,9 ohm og R320 = 80,6 ohm indstiller klasse 7. Regulatorerne til 5 V og 12 V er 2 A-varianter, men systemets specificerede samtidige belastning er 1 A på hver udgang; der er ikke tilføjet separate præcise 1 A-strømbegrænsere.

## Reference og bevidste ændringer

- ADI DC2476A-A: https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/dc2476a-a.html
- Aktuel manual: https://www.analog.com/media/en/technical-documentation/user-guides/dc2476a-a.pdf
- LT4295: https://www.analog.com/media/en/technical-documentation/data-sheets/LT4295.pdf
- LMR33620: https://www.ti.com/lit/ds/symlink/lmr33620.pdf — komponentværdier fra tabel 9-2 ved 400 kHz.
- Transformer: https://www.we-online.com/components/products/datasheet/750316231.pdf

Den detaljerede forbindelsestegning er transskriberet fra DC2476A-A schematic Rev. 1 (2016), hentet som producentens manual via en distributørkopi, og klasse 7-kapaciteten er kontrolleret mod den aktuelle Rev. C-manual. Den gamle BOM indeholder modstridende MOSFET-værdier; her følges schematic: BSC190N15NS3 på primærsiden og BSC320N20NS3 på sekundærsiden. Fysisk komponenttilgængelighed er ikke kontrolleret.

Auxiliary DC-input og prioritering er udeladt; AUX er bundet til primærstel. T2P-optokobleren får ekstern 3,3 V pull-up i stedet for referencekortets 24 V pull-up. POE_LOGIC_3V3 skal derfor tilsluttes værtsprocessorens 3,3 V. Den valgfrie isolationsoverbyggende kondensator er udeladt, indtil isolations- og EMC-krav er fastlagt. Referencekortets indgangs-common-mode-filter, RJ45/magnetics og terminering er ikke med i disse tre ark.

## Ikke klar til fremstilling

Dette er et elektrisk diagramudkast, ikke dokumentation for en afprøvet 41 W-forsyning. Før PCB-integration skal følgende færdiggøres:

1. **PoE++-Ethernet-indgang:** vælg og tegn et komplet RJ45-/magnetics-/termineringskredsløb med dokumenteret strømkapacitet for fire par. Det eksisterende Sheet_3 må ikke kobles direkte til J300 uden denne gennemgang. Kabelside-terminering skal blokere PoE-jævnstrøm.
2. **Effekttildeling:** T2P er ført ud, men ingen automatisk hardware-/firmwarestyring begrænser lasten ved lavere tildelt effekt. Værtsprocessor og/eller belastningsafbrydere skal sikre, at 41 W kun trækkes efter tilstrækkelig tildeling. Diagrammet alene løser ikke nedgradering til en mindre PoE-kilde.
3. **Komponenter og footprints:** konkrete kondensator-/spolevarianter, temperaturderating, tilgængelighed og footprints er ikke frigivet. Buck-spolerne skal mindst opfylde den angivne mætningsstrøm og valideres ved varm drift. 4 x 22 uF er nominelt; effektiv kapacitans ved DC-bias skal kontrolleres.
4. **Layout og isolationsafstande:** korte strømloops, Kelvin-forbindelse til R314, termiske pads, copper area, krybeafstand, isolation og indgangsfilter skal dimensioneres.
5. **Prototypeprøver:** opstart, samtidige lastspring, kortslutning, overtemperatur, ripple, 24 V-oversving ved lastafkast, EMC og PoE-interoperabilitet. Buck-regulatorernes maksimale anbefalede indgang er 36 V.

## Kontrol udført

- KiCad kan indlæse og eksportere de tre ark.
- 331 benforbindelser sammenlignet med generatorens tilsigtede netgrupper: ingen afvigelser. Dette kontrollerer tegningens netforbindelser, ikke analog funktion eller en uafhængig revision af referencekredsløbet.
- Ingen elektriske ERC-fund på de tre nye ark i en kontrolkopi, hvor komponenterne midlertidigt medtages i netlisten. Det eksisterende projekt har stadig sine tidligere kontrolfund og bibliotekshenvisninger.
- Nye brugerdefinerede symboler indeholder flere passive bentypninger, så ERC kan ikke alene validere alle funktioner.
- Ingen dublerede komponentreferencer.
- Bytekontrol bekræfter uændrede eksisterende child sheets og uændrede PCB-filer.
