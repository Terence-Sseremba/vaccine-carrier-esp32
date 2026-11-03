# Vaccine Carrier ESP32 — Firmware Starter

A limited **prototype code starter**, based on the 30 October 2026 Granola planning meeting.

## What this includes
- `src/main.cpp`: DS18B20 temperature reading, basic fault/range state machine, buzzer output, OLED display, Serial log of events.
- `include/config.h`: configurable temperature thresholds and **provisional** ESP32 DevKit pin assignments.
- `include/logic.h`: pure temperature classification for offline logic tests.
- `test/test_logic.cpp`: standalone C++ assertions for boundary and sensor-fault cases.
- `docs/SCHEMATIC_HANDOFF.md`: hardware questions and pin map to send to schematic teammate.

## What this does NOT include
Actual Wi-Fi notifications, SIM800 SMS, SD-card writes, time synchronization, buffering, alert delivery acknowledgement, or verified physical integration. The meeting discussed these, but the starter intentionally does not claim they are complete.

## Open in VS Code
1. Install the PlatformIO IDE extension.
2. Open this folder as a project.
3. Check `platformio.ini` and update `board` for the EXACT ESP32 variant.
4. Confirm wires, pull-ups, power supply and buzzer driver with schematic teammate before connection.
5. Use PlatformIO Build to compile after installing dependencies. Upload to real board only when wiring confirmed.

## Test standalone logic
With C++ compiler available:
```
g++ -std=c++17 -Iinclude test/test_logic.cpp -o test_logic
./test_logic
```

## Simulation
Wokwi supports ESP32 and DS18B20; Wokwi is useful for core logic experiments. Any simulation circuit must match validated pins. GSM/SMS still requires dedicated real-hardware tests.

## Safety and reliability
Do not use this project to protect real vaccines or make clinical decisions. Vaccine-specific storage limits, calibrated probes, traceable timestamps, alarms, backup power, monitoring continuity, cold-chain qualification and independent verification are required for deployment.
