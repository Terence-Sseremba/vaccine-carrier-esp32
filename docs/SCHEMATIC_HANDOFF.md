# Schematic handoff — provisional, review required

**Origin:** Granola "Project planning: vaccine carrier" (30 October 2026), https://notes.granola.ai/d/35593445-f885-46da-8ec9-37a2940026d2

## Agreed system goals from meeting summary
- ESP32 firmware, DS18B20 temperature probe, SSD1306-type OLED (model unconfirmed), buzzer, SD storage, SIM800 GSM and ESP32 Wi-Fi.
- Temperature range alarm, on-device display, recorded readings, and potential Wi-Fi/SMS alerts.
- Breadboard prototype, with separate physical testing of SMS.

## Provisional pin map in starter (not a final wiring diagram)
| Module | ESP32 pin | Important detail |
|---|---|---|
| DS18B20 data | GPIO4 | Requires suitable pull-up to 3.3V, commonly 4.7kΩ; verify probe cable colors against supplier documentation |
| OLED I2C SDA | GPIO21 | Assumes I2C OLED with address 0x3C; confirm board/voltage |
| OLED I2C SCL | GPIO22 | Shared I2C bus possible if later required |
| Active-high buzzer control | GPIO18 | Only if suitable low-current buzzer/driver; use transistor + protection when required |
| SIM800 UART TX/RX | TBD | Choose free safe UART pins; cross TX/RX and check logic levels |
| SD via SPI | TBD | Specify actual SD breakout, CS, SCK, MISO, MOSI, voltage levels |

## Critical schematic checks
1. Exact ESP32 board variant and module pinout (DevKit V1 / WROOM-32 assumed only for starter).
2. SIM800 model and breakout. Bare SIM800L VBAT needs approx. 3.4–4.4V (4.0V recommended) with peak current up to 2A. Use rated regulator, decoupling and common ground; do not power it directly from ESP32 GPIO or 3.3V rail. Verify UART logic voltage compatibility.
3. SIM800 uses GSM/2G: confirm the intended local mobile operator, frequency bands and available coverage BEFORE purchase and testing. SMS requires SIM, network and credit. Module may need PWRKEY sequencing.
4. Agree OLED controller/address (SSD1306 128x64 I2C assumed for starter).
5. Select appropriate SD breakout and ensure 3.3V-compatible signalling.
6. Confirm whether buzzer is active or passive, continuous or pulsed, and driving circuit.
7. Ensure suitable external power, battery/charging plan, and reliable ground arrangement.
8. Specify storage-range requirements based on vaccine type; 2–8°C is an initial illustrative choice only.
9. Clarify notification destination, transport (Wi-Fi service), remote acknowledgement and retry, real-time clock or trusted time synchronisation, and whether logging must survive outages.

## Prototype software status
- Implemented: sensor polling, fault / too-cold / normal / too-hot classification, OLED status, buzzer output, serial events.
- Not implemented: SD card logging, clock/timestamp acquisition, Wi-Fi endpoint, SMS fallback, hardware-specific SIM800 UART commands, retries and delivery acknowledgement.
- Code has not been built for an ESP32 board and has not been run with hardware. Test assertions can be run separately using a host compiler.
- Firmware must not be described as a verified end-to-end monitoring product or used for actual vaccine cold chain management.
