#pragma once
#include <stdint.h>

// Provisional ESP32-WROOM-32 development-board mapping. Verify the actual board
// and peripheral breakouts before wiring or powering the prototype.
constexpr int PIN_ONEWIRE = 4;
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;
constexpr int PIN_GSM_RX = 16;  // ESP32 RX <- SIM800L TX
constexpr int PIN_GSM_TX = 17;  // ESP32 TX -> SIM800L RX (voltage matching required)

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 32;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;  // Verify on the physical module.

constexpr float TEMP_MIN_C = 2.0f;
constexpr float TEMP_MAX_C = 8.0f;
constexpr uint32_t READ_INTERVAL_MS = 2000U;
constexpr uint32_t STATUS_INTERVAL_MS = 10000U;
constexpr uint32_t ALERT_REPEAT_MS = 60000U;
constexpr uint32_t SMS_REPEAT_MS = 300000U;
// SIM800L breakout, UART baud and power-on method require hardware validation.
constexpr uint32_t GSM_UART_BAUD = 9600U;
