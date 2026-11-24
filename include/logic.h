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

struct AlertDecision {
  bool stateChanged;
  bool alarmActive;
  bool reminderDue;
};

struct AlertTracker {
  TempState previousState = TempState::SensorFault;
  bool hasPrevious = false;
  unsigned long lastReminder = 0;
  bool reminderScheduled = false;
};

inline AlertDecision decideAlert(TempState current, TempState previous,
                                 bool hasPrevious, unsigned long now,
                                 unsigned long lastReminder,
                                 bool reminderScheduled,
                                 unsigned long repeatMs) {
  const bool changed = !hasPrevious || current != previous;
  const bool active = current != TempState::Normal;
  const bool reminder = active &&
      (changed || !reminderScheduled || now - lastReminder >= repeatMs);
  return {changed, active, reminder};
}

inline AlertDecision updateAlert(AlertTracker& tracker, TempState current,
                                 unsigned long now, unsigned long repeatMs) {
  const AlertDecision decision = decideAlert(
      current, tracker.previousState, tracker.hasPrevious, now,
      tracker.lastReminder, tracker.reminderScheduled, repeatMs);
  tracker.previousState = current;
  tracker.hasPrevious = true;
  if (!decision.alarmActive) {
    tracker.reminderScheduled = false;
  } else if (decision.reminderDue) {
    tracker.lastReminder = now;
    tracker.reminderScheduled = true;
  }
  return decision;
}
