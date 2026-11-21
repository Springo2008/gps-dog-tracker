# GPS-hundesporer med LoRa

En hjemmelaget hundesporer med to ESP32-baserte enheter: en sender på hunden og en håndholdt mottaker med skjerm. Senderen henter posisjon fra GPS og sender den direkte til mottakeren via LoRa, uten mobilnett.

**Prosjektet er en prototype.** Ikke stol på det som eneste måte å finne hunden på. Test GPS, rekkevidde, strømforsyning og batteritid grundig før bruk. Programvaren er ikke fullstendig testet på maskinvaren.

## Deler

| Del | Antall | Bruk |
|---|---:|---|
| Heltec Wireless Stick Lite V3, 868 MHz (ESP32-S3 og SX1262) | 2 | Sender og mottaker |
| ATGM336H GPS-modul med antenne | 2 | Posisjon på begge enheter |
| 868 MHz antenne med riktig adapter/kabel til kortet | 2 | LoRa |
| 1,8" ST7735S SPI TFT, 128 × 160 | 1 | Mottaker |
| 3,7 V LiPo-batteri med passende kontakt | 2 | Strøm |
| Ledninger, eventuelle brytere og kabinetter | Etter behov | Montering |

Et BNO055-kompass er **ikke i bruk** i denne versjonen. STEP-filer til kabinettene legges til senere.

## Kobling

Koble fra strømmen mens du kobler. GPIO-numrene under er ESP32-numre, ikke nødvendigvis fysiske pinnumre. Sjekk pinout for akkurat din kortversjon.

### GPS på begge enheter

| GPS | Heltec |
|---|---|
| VCC | 3,3 V, dersom GPS-breakoutet støtter 3,3 V |
| GND | GND |
| TXD | GPIO18 |
| RXD | GPIO17 |

GPS bruker 9600 baud. Kontroller spenningskravene for ditt GPS-kort før tilkobling. På dette oppsettet ga 5 V-pinnen ikke strøm til GPS når kortet gikk på batteri; test derfor at GPS faktisk er strømforsynt både på USB og batteri.

### Skjerm på mottakeren

| ST7735S | Heltec |
|---|---|
| GND | GND |
| VCC / VDC | 3,3 V |
| SCL / SCK | GPIO47 |
| SDA / MOSI | GPIO35 |
| RES / RST | GPIO38 |
| DC | GPIO48 |
| CS | GPIO36 |
| BLK | GPIO26 |

SCL og SDA på denne skjermen er SPI-signaler, ikke I²C. Sjekk at skjermens baklys og strømtilkobling er kompatible med 3,3 V og kortet ditt. `display_test` kan brukes for å sjekke skjermen før hele mottakeren lastes opp.

LoRa-radioen er innebygd og bruker GPIO8–14 ifølge koden; den trenger ingen ekstern SPI-kobling. Fest antennene før du slår på og sender.

### Batteri

Bruk et egnet **encellet** LiPo-batteri og kontroller kontakt, polaritet og kortets ladekrets før tilkobling. En bryter kan settes i batteriets plussledning; da kan batteriet normalt bare lades når bryteren er på. Unngå kortslutning og bruk egnet batteribeskyttelse.

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
