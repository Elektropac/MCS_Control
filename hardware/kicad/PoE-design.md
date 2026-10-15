# PoE 5 V / 2 A — revision B, diagramprototype

Det aktive ark er PoE_5V. Det erstatter de tre tidligere PoE-ark. Kun én isoleret 5 V-udgang, mål 1–2 A (op til 10 W). Alle forbindelser er tegnet som ledninger. Kryds med prik er forbundet; kryds uden prik er ikke forbundet. Gamle 41 W-ark er gemt under backup/PoE-41W-revA.

## Kredsløb og reference

Baseret på TI PMP7355 Rev. B, et 5 V / 2 A referencekredsløb til IEEE 802.3af Type 1, Class 3. TPS23753APW håndterer detektion, klassifikation, hot-swap og flybackstyring. To HD01-broer giver polaritetsuafhængig indgang. FDS2582 og Coilcraft POE13P-50L overfører energien over isolationen. B540 ensretter sekundæren; LC-filteret leverer 5 V. TLV431A og TCMT1107 regulerer udgangen via optokobling.

Kilder: [TI referenceprojekt](https://www.ti.com/tool/PMP7355), [diagram](https://www.ti.com/lit/pdf/SLVRB20), [BOM](https://www.ti.com/lit/pdf/SLVRB21), [controller](https://www.ti.com/lit/ds/symlink/tps23753a.pdf), [BAS16-ben](https://assets.nexperia.com/documents/data-sheet/BAS16_SER.pdf).

Referencer er forskudt med 400 fra TI-tegningen. Auxiliary adapterindgang og tilhørende netværk er udeladt; APD er forbundet til RTN. VSS og RTN er separate net med controllerens interne switch imellem. Modstandsdeleren giver nominelt cirka 5,07 V. J403: ben 1 = 5 V, ben 2 = isoleret GND.

## Afgrænsning og næste konstruktionstrin

J400 er alene grænsefladen til Ethernet-strømudtag. Den er endnu ikke forbundet til Sheet_3. Ethernet-magnetics, strømkapacitet, kabelterminering og korrekt PoE-udtag skal kontrolleres ved integration. De andre diagramark og begge PCB-filer er uændrede. Alle nye komponenter er udeladt fra PCB.

Dette er et diagramudkast, ikke et produktionsklart eller fysisk afprøvet design. Endelige bestillingsnumre og footprints mangler for flere komponenter. C406/C427 kræver passende certificeret isolationskondensator og kontrol af isolation/lækstrøm; en spændingsmærkning alene er ikke tilstrækkelig. Transformer, magnetiske komponenter, strømgrænser, layout, termik, EMC, start og belastningsspring skal verificeres på hardware ved 1 A og 2 A. Referencekredsløbets testresultater er ikke en test af vores tilpasning.

## Kontrol

57 komponenter og 151 ben kontrolleret mod de tilsigtede forbindelsesgrupper ved KiCad-netlisteeksport: ingen afvigelser, ingen dublerede referencer. Det nye ark har ingen ERC-fund med projektets eksisterende indstillinger. Øvrige ark har eksisterende ERC-fund. Netlistekontrollen verificerer tegningens forbindelser, ikke den analoge funktion. Passive symbolbentyper begrænser ERC-dækningen. PCB-hashkontrol viser begge PCB-filer uændrede.
