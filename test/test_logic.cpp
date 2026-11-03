// Host-side standalone logic assertions: g++ -std=c++17 -Iinclude test/test_logic.cpp -o /tmp/test_logic && /tmp/test_logic
#include <cassert>
#include <limits>
#include "logic.h"
int main() {
 assert(classifyTemperature(2.0f)==TempState::Normal);
 assert(classifyTemperature(8.0f)==TempState::Normal);
 assert(classifyTemperature(1.9f)==TempState::TooCold);
 assert(classifyTemperature(8.1f)==TempState::TooHot);
 assert(classifyTemperature(-127.0f)==TempState::SensorFault);
 assert(classifyTemperature(85.0f)==TempState::SensorFault);
 assert(classifyTemperature(std::numeric_limits<float>::quiet_NaN())==TempState::SensorFault);
 assert(classifyTemperature(0.0f)==TempState::TooCold);
 assert(classifyTemperature(20.0f)==TempState::TooHot);
 return 0;
}
