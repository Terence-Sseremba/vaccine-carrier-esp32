#pragma once

#include <Arduino.h>
#include "sms_transport.h"

class Sim800Uart : public SmsUart {
 public:
  explicit Sim800Uart(HardwareSerial& serial) : serial_(serial) {}
  void begin(uint32_t baud, int rxPin, int txPin);
  void service(SmsTransport& transport, uint32_t now, uint16_t byteBudget = 128);
  void write(const char* data, size_t length) override;
  void writeByte(uint8_t byte) override;

 private:
  HardwareSerial& serial_;
};
