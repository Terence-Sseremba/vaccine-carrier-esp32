# VC-004 hardware handoff — provisional development settings

This is an educational prototype, not a validated medical device. The original
teammate KiCad file is preserved locally at `.vc002-reference/test/IEEE work.kicad_sch`.
It contains four component symbols (ESP32-WROOM-32, DS18B20, SIM800L and an
ER-OLEDM0.91-1x-I2C OLED), but **no wires, nets, power circuit or pull-ups**.
The pin table below is a firmware proposal, not a verified schematic connection.

| Peripheral | Provisional ESP32 pin or setting | Verify before wiring |
| --- | --- | --- |
| DS18B20 DQ | GPIO4 | Probe lead colors; powered three-wire connection; approximately 4.7 kΩ DQ pull-up to 3.3 V |
| OLED SDA / SCL | GPIO21 / GPIO22 | Actual breakout pinout, 3.3 V supply, I2C pull-ups |
| OLED controller / size | SSD1306, 128×32 | Actual display model; the old firmware assumed 128×64 |
| OLED I2C address | 0x3C | Scan/verify on the actual breakout |
| SIM800L TXD → ESP32 RX | GPIO16 | Actual board pin availability and UART voltage matching |
| ESP32 TX → SIM800L RXD | GPIO17 | Level matching for the chosen modem breakout |
| Serial diagnostics | UART0 / USB interface | Actual development-board USB-to-UART arrangement |

The PlatformIO target remains `esp32dev` until the exact ESP32-WROOM-32
development board is identified. A bare WROOM-32 module needs its own power,
reset/boot and programming circuitry; it is not interchangeable with a complete
development board. Keep the modem on a separate hardware UART from Serial.
Cross TX to RX and join grounds. Do not connect a bare SIM800L UART directly
until its 2.8 V interface and the ESP32's 3.3 V signals have been matched.

## Power and assembly checks

- Identify the exact development board, DS18B20 waterproof probe, OLED breakout,
  SIM800L breakout, and power converter by part number or clear photographs.
- Verify that the DS18B20 is powered at 3.3 V and its DQ pull-up reaches 3.3 V,
  not a higher voltage on an ESP32 input. Check probe wire colors against its
  supplier, not a generic color convention.
- A bare SIM800L needs 3.4–4.4 V at VBAT (4.0 V recommended), with transmit
  bursts up to 2 A. Select a converter and source with sufficient margin and
  add low-ESR decoupling close to VBAT as described in the SIMCom hardware guide.
  A breakout may already contain some of this circuitry; inspect the actual one.
- Determine how the chosen breakout exposes PWRKEY and how it is started. A
  bare SIM800L requires a PWRKEY low pulse of more than one second for power-on.
- Verify common grounding, antenna, active SIM, SMS service, and local 2G band
  and coverage before attempting end-to-end messaging.

## Firmware state and SMS transport contract

VC-002 local Alert Policy A remains active with a 60-second reminder interval.
Sampling is every 2 seconds; temperature limits default to 2.0–8.0 °C.
The current DallasTemperature conversion can block for roughly 750 ms at
12-bit resolution. VC-005 must keep modem command processing bounded so sensor
and OLED updates continue during GSM failures.

VC-004 generates board-independent SMS **events**: immediate on each abnormal
transition, every 5 minutes while an abnormal state is stable, and once on
recovery to Normal. Events are logged to Serial as `NOT SENT`. No SMS transport,
recipient, transmission attempt or modem-confirmed submission exists yet.

`SmsOutbox` is an optional fixed eight-event queue for VC-005. If full, it
evicts the oldest event and counts that loss so the newest state is retained.
Events at least 15 minutes old are discarded and counted as stale. Dequeuing
hands an event to the future transport; it does **not** mark an SMS attempt or
success. VC-005 should hold the active message separately, allow at most two
bounded attempts with at least 30 seconds between failed attempts, and record
modem submission only after a positive final AT response. A failed attempt
must never trigger an unbounded retry loop. Submission to the modem does not
prove delivery to the recipient. Keep the recipient in ignored local
configuration, never committed source.

The OLED uses a compact 128×32 layout and reserves a `GSM --` field for a
future transport state. A display initialization failure leaves sensor and
Serial monitoring active. The buzzer is excluded. SD logging, Wi-Fi
notifications and voice calls are deferred and have no firmware dependency.

## Required physical validation

Before calling the prototype integrated, confirm boot with all peripherals
attached; sensor disconnect and fresh-conversion recovery; cold, hot and fault
display states; OLED address and geometry; modem supply voltage during transmit
bursts; PWRKEY startup; AT communication; SMS submission with a test SIM; and
continued temperature monitoring through a network outage. Record the exact
board and breakout models and update this provisional table accordingly.
