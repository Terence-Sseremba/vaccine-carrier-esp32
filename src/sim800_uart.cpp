#include "sim800_uart.h"

void Sim800Uart::begin(uint32_t baud, int rxPin, int txPin) {
  serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
}

void Sim800Uart::service(SmsTransport& transport, uint32_t now, uint16_t byteBudget) {
  while (byteBudget-- && serial_.available() > 0)
    transport.onByte(static_cast<char>(serial_.read()), now, *this);
}

void Sim800Uart::write(const char* data, size_t length) {
  serial_.write(reinterpret_cast<const uint8_t*>(data), length);
}

void Sim800Uart::writeByte(uint8_t byte) { serial_.write(byte); }
