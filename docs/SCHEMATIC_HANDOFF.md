# VC-005 hardware handoff — provisional development settings

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
The DallasTemperature request now starts a cooperative conversion. The loop
continues servicing modem UART while waiting 750 ms before reading the 12-bit
result. Sensor rediscovery still requires a fresh conversion before recovery.

VC-004 still generates board-independent SMS **events**: immediate on each
abnormal transition, every 5 minutes while abnormal, and once on recovery to
Normal. VC-005 queues those events and uses UART2 for cooperative SIM800L AT
communication. Copy `include/local_sms_config.example.h` to the Git-ignored
`include/local_sms_config.h` and set `SMS_RECIPIENT` to an international number
with an optional leading `+` and 6–15 digits. Missing or invalid configuration
disables SMS submission while local monitoring continues. Never commit or log
a real recipient, SIM identifier or credential.

`SmsOutbox` is a fixed eight-event queue. If full, it
evicts the oldest event and counts that loss so the newest state is retained.
Events at least 15 minutes old are discarded and counted as stale. Dequeuing
transfers an event into one separately owned active slot; it does **not** mark
an attempt or success. The transport permits at most two attempts per event,
at least 30 seconds apart. It checks `AT`, `ATE0`, `AT+CPIN?`, `AT+CREG?`, and
`AT+CMGF=1` before `AT+CMGS`. It accepts home and roaming registration.
After the `>` prompt it sends text and Ctrl-Z, then requires `+CMGS` and final
`OK` for `SUBMITTED`. That means modem acknowledgement, **not** recipient
delivery. The final response wait is cooperative and bounded at 65 seconds.
Explicit transient rejection may be retried once. Known permanent CMS errors
halt the transport in `FAIL` until configuration/operator intervention. A
timeout after Ctrl-Z is `AMBIGUOUS` and never retries that event. A pre-payload timeout sends ESC,
reprobes the modem, and observes retry spacing.

On reconnection the firmware waits for a fresh temperature sample, removes
obsolete queued states, and reconciles with the live state. Persistent high,
low or sensor-fault states can produce a current-state event even if historical
events expired. Recovery is sent only when an abnormal state in that episode
was previously acknowledged by the modem. A state change before payload
cancels the old request; after payload its outcome is observed before new-state
reconciliation. Ambiguous abnormal submissions suppress immediate synthetic
resend for five minutes; an ambiguous recovery is not regenerated for the same
episode. Equivalent reminders are coalesced while queued or active. Episode
history is RAM-only and is lost on reboot.

The OLED uses a compact 128×32 layout and reserves a small modem status field
(`OFF`, `INIT`, `WAIT`, `READY`, `SEND`, `ACCEPT`, `FAIL`, `UNCERT`). `ACCEPT`
means modem acknowledgement only. A display initialization failure leaves sensor and
Serial monitoring active. The buzzer is excluded. SD logging, Wi-Fi
notifications and voice calls are deferred and have no firmware dependency.

The prototype assumes an already powered modem and an unlocked SIM. It does
not drive PWRKEY or enter a SIM PIN. Text messages use short ASCII content; it
does not implement multipart SMS, delivery reports, persistent retry history,
or recipient delivery confirmation. Physical UART and SMS operation remain
unverified until tested with the chosen breakout, supply, SIM and network.

## Required physical validation

Before calling the prototype integrated, confirm boot with all peripherals
attached; sensor disconnect and fresh-conversion recovery; cold, hot and fault
display states; OLED address and geometry; modem supply voltage during transmit
bursts; PWRKEY startup; AT communication; SMS submission with a test SIM; and
continued temperature monitoring through a network outage. Record the exact
board and breakout models and update this provisional table accordingly.
