#pragma once

// PROVISIONAL pin map for classic ESP32 DevKit (not yet agreed with schematic teammate)
// Review boot/strapping pins and peripheral requirements with the actual module datasheet.
constexpr int PIN_ONEWIRE = 4;
constexpr int PIN_BUZZER = 18;
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;

// SIM800 and SD card pins intentionally UNASSIGNED until final modules / schematic confirmed.
// This starter does NOT send notifications or write to SD. It reports these as pending.
constexpr float TEMP_MIN_C = 2.0f;
constexpr float TEMP_MAX_C = 8.0f;
constexpr unsigned long READ_INTERVAL_MS = 2000UL;
constexpr unsigned long STATUS_INTERVAL_MS = 10000UL;
constexpr unsigned long ALERT_REPEAT_MS = 60000UL;
