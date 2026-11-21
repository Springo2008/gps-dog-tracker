# GPS-hundesporer med LoRa

En hjemmelaget hundesporer med to ESP32-baserte enheter: en sender på hunden og en håndholdt mottaker med skjerm. Senderen henter posisjon fra GPS og sender den direkte til mottakeren via LoRa, uten mobilnett.

**Prosjektet er en prototype.** Ikke stol på det som eneste måte å finne hunden på. Test GPS, rekkevidde, strømforsyning og batteritid grundig før bruk. Programvaren er ikke fullstendig testet på maskinvaren.

## Deler

| Del | Antall | Bruk |
|---|---:|---|
| Heltec Wireless Stick Lite V3 (ESP32-S3 og SX1262), bestilt variant oppgitt som 863–928 MHz | 2 | Sender og mottaker; koden bruker 869,525 MHz |
| GPS/BDS-modul markedsført som ATGM336H-erstatning, med antenne | 2 | Posisjon på begge enheter; verifiser modulens UART og pinout |
| 868 MHz SMA-hannantenne | 2 | LoRa; eventuell adapter til Heltec-kortet må passe kortets RF-kontakt |
| 1,8" ST7735S SPI TFT, 128 × 160, oppgitt 3,3 V | 2 | Én brukes på mottakeren; én ekstra |
| 3,7 V 3000 mAh LiPo-batteri, oppgitt med PH2.0-kontakt | 3 | To aktuelle enheter og ett ekstra; kontaktkompatibilitet med Heltec må bekreftes |
| 40-leder flatkabel, 1,27 mm deling | 2 m | Ledningsmateriale; krever passende terminering/adapter |
| Vanntett USB-C panelkontakt, 2 ledere | 10 | Ekstern strømtilkobling i kabinett; kobling må verifiseres før bruk |
| 2-pols vippebryter | 5 | Mulig av/på-bryter; DC-egnethet må verifiseres |
| M3 varmeinnsatser i messing | 420 stk. | Mekanisk montering i kabinettene, ikke elektrisk kobling |
| Skruer, valgt variant BT2 | 1 sett | Mekanisk montering; kontroller dimensjon og lengde mot kabinettet |
| Ledninger og kabinetter | Etter behov | Montering |

Et BNO055-kompass er **ikke i bruk** i denne versjonen. STEP-filer til kabinettene legges til senere.

## Kobling

Koble fra USB og batteri før du kobler ledninger. Det er to separate Heltec Wireless Stick Lite V3-kort: ett for senderen og ett for mottakeren. `GPIO17`, `GPIO18` osv. nedenfor er ESP32-GPIO-numre som brukes av programkoden, ikke fysiske pinnumre eller en rekkefølge på kortets kontakter. Finn den tilsvarende GPIO-merkede loddeputen/pinnen i pinout for akkurat din kortrevisjon; ikke bruk plasseringen i en pinne- eller headerrekke som identifikasjon.

### GPS på senderen (TX)

Koble GPS-modulen til Heltec-kortet som kjører `tx/tx.ino`:

| Pinne på GPS-modulen | Tilkobling på senderens Heltec |
|---|---|
| TXD | GPIO18 (GPS-data inn til ESP32) |
| RXD | GPIO17 (GPS-data ut fra ESP32) |
| GND | GND |
| VCC | 3V3 bare dersom GPS-modulens dokumentasjon bekrefter at denne inngangen støtter 3,3 V. Hvis den krever en annen forsyning eller pinnen er merket VIN, avklar dette før tilkobling. |

### GPS på mottakeren (RX)

Koble den andre GPS-modulen til Heltec-kortet som kjører `rx/rx.ino`:

| Pinne på GPS-modulen | Tilkobling på mottakerens Heltec |
|---|---|
| TXD | GPIO18 (GPS-data inn til ESP32) |
| RXD | GPIO17 (GPS-data ut fra ESP32) |
| GND | GND |
| VCC | 3V3 bare dersom GPS-modulens dokumentasjon bekrefter at denne inngangen støtter 3,3 V. Hvis den krever en annen forsyning eller pinnen er merket VIN, avklar dette før tilkobling. |

På begge enheter krysser UART-dataretningene: GPS `TXD` går til Heltec `GPIO18`, og GPS `RXD` går til Heltec `GPIO17`. Dette er de GPIO-numrene skissene setter opp for 9600 baud. Den bestilte modulen er markedsført som en ATGM336H-erstatning; produktteksten alene bekrefter ikke modulens eksakte pinout, forsyningsspenning, UART-logikknivå eller standard baudrate. Kontroller dette mot dokumentasjonen/merkingen på modulen før tilkobling, og ikke anta at ulike GPS-breakout-kort har samme regulator eller pinnemerking. I dette oppsettet ga Heltec-kortets 5V-pinne ikke strøm til GPS når kortet gikk på batteri. Verifiser derfor GPS-strøm og funksjon både på USB og batteri før bruk.

### TFT-skjerm på mottakeren

Koble skjermen bare til mottakerens Heltec-kort. `SCL` og `SDA` er navn skjermen kan bruke for SPI-klokke og SPI-data; de betyr ikke at denne koblingen bruker I²C.

| Pinne på ST7735S-skjermen | Tilkobling på mottakerens Heltec |
|---|---|
| GND | GND |
| VCC / VDC | Heltec 3V3 (den bestilte modulen er oppgitt som 3,3 V; kontroller merkingen på modulen) |
| SCL / SCK | GPIO47 (SPI-klokke) |
| SDA / MOSI | GPIO35 (SPI-data fra Heltec til skjerm) |
| RES / RST | GPIO38 (reset) |
| DC | GPIO48 (data/kommando) |
| CS | GPIO36 (chip select) |
| BLK | GPIO26 (baklys-styring; kontroller at skjermens BLK-inngang tåler GPIO-signalnivået) |

GPIO-numrene for skjermen kommer fra `rx/rx.ino` og `display_test/display_test.ino`. Sjekk skjermens pinnebetegnelser, forsyningskrav og signalnivåer før du kobler til; skjermvarianter kan ha ulike kontakter og strømkrav. `display_test` kan brukes for å sjekke skjermen før hele mottakeren lastes opp.

### Antenner

- Hvert Heltec-kort har innebygd LoRa-radio. Koden bruker GPIO8–14 internt til radioen; disse signalene skal ikke kobles til en ekstern GPS eller skjerm.
- De bestilte LoRa-antennene er oppgitt som 868 MHz med SMA-hannkontakt. Koble hver antenne til sitt Heltec-kort bare med en adapter som passer kortets RF-kontakt og er beregnet for antennen. Ikke tving sammen kontakter som ikke passer; fest antennen før du slår på radioen eller sender.
- Koble GPS-antennen til GPS-modulens egen antennekontakt, hvis modulen bruker en separat antenne. GPS-antennen kobles ikke til Heltec-kortets LoRa-antennekontakt.

### Batteri

Hvert Heltec-kort har sitt eget batteri; sender og mottaker deler ikke batteri. De bestilte cellene er oppgitt som encellede 3,7 V 3000 mAh LiPo med PH2.0-kontakt. Dette bekrefter ikke at kontakten passer Heltec-kortet eller at polariteten er lik. Bruk dem bare dersom kortets batteriinngang og batteridokumentasjon bekrefter kompatibilitet. Koble batteriet bare til batteriinngangen som er identifisert i dokumentasjonen og merkingen for akkurat kortrevisjon; repoet angir ikke fysisk kontakt eller pinneplassering. Kontroller kontakt og polaritet før tilkobling. Ikke koble batteriet til GPIO-pinner eller anta at en vilkårlig 3V3-/5V-pin er batteriinngang.

Programmet leser kortets innebygde batterimålekrets: GPIO1 måler spenningsdeleren, og GPIO37 styrer målingen. Dette er interne målesignaler, ikke ekstra batteriledninger. Kontroller kortets kontakt, polaritet og ladekrets før tilkobling. Unngå kortslutning, bruk egnet batteribeskyttelse, og ikke lad et batteri uten å ha bekreftet at kortets ladekrets er beregnet for det. En bryter i batteriets plussledning kan påvirke ladingen; kontroller dette for den konkrete koblingen.

### Panelmontert USB-C-kontakt og bryter

Den bestilte panelkontakten er oppgitt som USB-C med **to ledere**. To ledere gir ikke USB-data, så kontakten kan ikke brukes til å programmere kortet. Produktteksten alene dokumenterer ikke ledernes polaritet, eventuell USB-C-konfigurasjon eller at kontakten kan brukes som ladeinngang til Heltec-kortet.

Ikke koble panelkontakten direkte til batteriet, GPIO-pinner eller 5V-pinnen før kontakten og kortets inngang er identifisert og dokumentert. Bruk Heltecs innebygde USB-C-kontakt til programmering. En ekstern 2-lederkontakt kan først kobles til strøm etter at du har bekreftet hvilken leder som er pluss/minus, hvilken spenning den leverer, og at kortets valgte inngang støtter dette. Ikke bruk den som batterilader uten å bekrefte at ladebanen er beregnet for det.

Vippebryterne kan eventuelt brukes som av/på-brytere, men produkttekstens strømrating alene bekrefter ikke at den aktuelle bryteren er egnet for LiPo-likestrøm. Kontroller DC-rating og polaritet før den eventuelt settes i serie med batteriets plussledning. Inntil dette er bekreftet, skal hverken USB-C-panelkontakten eller bryteren regnes som en ferdig koblingsanvisning.

## Programvare og opplasting

1. Installer [Arduino IDE](https://www.arduino.cc/en/software/).
2. Legg til `https://espressif.github.io/arduino-esp32/package_esp32_index.json` under **Preferences → Additional Boards Manager URLs**, og installer **esp32 by Espressif Systems** i Boards Manager.
3. Installer bibliotekene **RadioLib**, **TinyGPSPlus**, **Adafruit GFX Library** og **Adafruit ST7735 and ST7789 Library** i Library Manager. Installer avhengighetene IDE-en ber om.
4. Velg **Heltec Wireless Stick Lite(V3)** under ESP32-kort og velg riktig port. Kopier eventuelt innstillingene fra ditt fungerende oppsett.
5. Åpne `tx/tx.ino` og last opp til hundens enhet. Åpne `rx/rx.ino` og last opp til håndholdt enhet. `display_test/display_test.ino` er en separat skjermtest.
6. Åpne Serial Monitor på **115200 baud** for GPS- og LoRa-status. Bruk USB-tilkoblingen på kortet for programmering; en ekstern USB-kontakt med bare strømledninger kan ikke overføre programmet.

## Slik fungerer det

- Hunden sender koordinater når GPS har en fersk posisjon. Satellitter og HDOP sendes hver femte pakke, og batteristatus hver tiende pakke. Uten fersk GPS-posisjon sendes en statusmelding i stedet.
- Senderen bruker 869,525 MHz, 125 kHz båndbredde og SF10 ved registrert bevegelse; etter 30 sekunder uten registrert bevegelse går den over til SF12. Faktisk sendeintervall avhenger av pakkens lengde og lufttid.
- Mottakeren viser avstand og retning til hunden. Ved gange bruker den GPS-kurs; når man står stille er pilen nordorientert, ikke kompassorientert.
- Siste mottatte hundeposisjon lagres i flash. En gammel eller lagret posisjon er ikke nødvendigvis der hunden er nå.
- Batteriprosent er et anslag basert på spenning. Ladeindikatoren er en usikker heuristikk, ikke en bekreftet ladestatus.

LoRa-innstillingene er laget for dette oppsettet. Undersøk gjeldende regler i landet der du bruker det, inkludert frekvens, sendetid og faktisk utstrålt effekt med antenne og kabel. Posisjonsdata sendes uten kryptering i denne versjonen.

## Feilsøking

- **DOG NO GPS:** Senderen har kontakt, men mangler fersk GPS-posisjon. Kontroller strøm, GPS-kobling og prøv utendørs med fri sikt til himmelen.
- **NO SIGNAL / LORA ERR:** Sjekk antenner, strøm og at begge enhetene kjører riktig skisse og samme radioinnstillinger. Se Serial Monitor for feilmeldinger.
- **GPS chars=0:** Kontroller GPS-strøm, felles GND og at GPS TXD er koblet til GPIO18.
- **Mørk eller feil skjerm:** Kontroller koblinger og kjør skjermtesten. Noen skjermvarianter kan kreve en annen `initR`-innstilling.
- **Feil retning i ro:** Mottakeren har ikke kompass; nord er opp når du står stille.
