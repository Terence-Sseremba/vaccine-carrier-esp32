#pragma once
#include <math.h>

enum class TempState { SensorFault, TooCold, Normal, TooHot };

inline TempState classifyTemperature(float t, float minC=2.0f, float maxC=8.0f) {
  // DS18B20 disconnect value is commonly -127C, and 85C may indicate a startup/conversion fault.
  // Here 85C is treated as an untrusted value. A legitimate temperature >8C should be TooHot.
  if (!isfinite(t) || t <= -126.0f || t == 85.0f) return TempState::SensorFault;
  if (t < minC) return TempState::TooCold;
  if (t > maxC) return TempState::TooHot;
  return TempState::Normal;
}

inline const char* stateText(TempState s) {
  switch (s) {
    case TempState::SensorFault: return "SENSOR ERROR";
    case TempState::TooCold: return "LOW TEMP";
    case TempState::Normal: return "NORMAL";
    case TempState::TooHot: return "HIGH TEMP";
  }
  return "UNKNOWN";
}
