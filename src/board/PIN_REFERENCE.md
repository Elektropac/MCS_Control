# Pin Reference — Waveshare ESP32-S3-ETH (v1)

Verificeret mod Control PCB schematic 2026-08-13.

## Header pins (40-pin)

| Pin | GPIO/Funktion | Tildeling         |
|-----|---------------|-------------------|
| 1   | GPIO20        | NC (USB D+)       |
| 2   | GPIO19        | NC (USB D-)       |
| 3   | GND           | -                 |
| 4   | GPIO48        | A1_DIGITAL_MCU    |
| 5   | GPIO47        | B4_DIGITAL_MCU    |
| 6   | GPIO46        | NC                |
| 7   | GPIO45        | NC                |
| 8   | GND           | -                 |
| 9   | GPIO42        | B3_DIGITAL_MCU    |
| 10  | GPIO41        | B2_DIGITAL_MCU    |
| 11  | GPIO40        | OLED DC           |
| 12  | GPIO39        | OLED RST          |
| 13  | GND           | -                 |
| 14  | GPIO38        | OLED CLK          |
| 15  | GPIO37        | OLED DIN (MOSI)   |
| 16  | GPIO36        | OLED CS           |
| 17  | GPIO35        | B1_DIGITAL_MCU    |
| 18  | GND           | -                 |
| 19  | GPIO34        | SCL (I2C)         |
| 20  | GPIO33        | SDA (I2C)         |
| 21  | GPIO43        | UART_B_TX         |
| 22  | GPIO44        | UART_B_RX         |
| 23  | GND           | -                 |
| 24  | GPIO0         | NC (boot-strap)   |
| 25  | GPIO1         | UART_A_TX         |
| 26  | GPIO2         | UART_A_RX         |
| 27  | GPIO3         | NC                |
| 28  | GND           | -                 |
| 29  | GPIO15        | Buttonpress       |
| 30  | CHIP_PU       | NC                |
| 31  | GPIO18        | A4_DIGITAL_MCU    |
| 32  | GPIO16        | A3_DIGITAL_MCU    |
| 33  | GND           | -                 |
| 34  | GPIO17        | A2_DIGITAL_MCU    |
| 35  | GPIO21        | Buzzer            |
| 36  | +3V3          | Strøm             |
| 37  | 3V3_EN        | -                 |
| 38  | GND           | -                 |
| 39  | VSYS          | NC                |
| 40  | VBUS          | +5V               |

## Interne (W5500 Ethernet — fast på boardet)

| GPIO | Funktion |
|------|----------|
| 12   | ETH MISO |
| 11   | ETH MOSI |
| 13   | ETH SCK  |
| 14   | ETH CS   |
| 9    | ETH RST  |
| 10   | ETH INT  |

## Interne (SD-kort — fast på boardet)

| GPIO | Funktion |
|------|----------|
| 5    | SD MISO  |
| 6    | SD MOSI  |
| 7    | SD CLK   |
| 4    | SD CS    |
