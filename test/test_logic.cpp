// Host-side assertions: g++ -std=c++17 -Iinclude test/test_logic.cpp -o test_logic
#include <cassert>
#include <limits>
#include "logic.h"

int main() {
  assert(classifyTemperature(2.0f) == TempState::Normal);
  assert(classifyTemperature(8.0f) == TempState::Normal);
  assert(classifyTemperature(1.9f) == TempState::TooCold);
  assert(classifyTemperature(8.1f) == TempState::TooHot);
  assert(classifyTemperature(0.0f) == TempState::TooCold);
  assert(classifyTemperature(20.0f) == TempState::TooHot);
  assert(classifyTemperature(-127.0f) == TempState::SensorFault);
  assert(classifyTemperature(-126.0f) == TempState::SensorFault);
  assert(classifyTemperature(85.0f) == TempState::SensorFault);
  assert(classifyTemperature(std::numeric_limits<float>::quiet_NaN()) == TempState::SensorFault);
  assert(classifyTemperature(std::numeric_limits<float>::infinity()) == TempState::SensorFault);
  assert(classifyTemperature(-std::numeric_limits<float>::infinity()) == TempState::SensorFault);

  constexpr unsigned long repeatMs = 60000UL;
  AlertTracker tracker;
  auto alert = updateAlert(tracker, TempState::SensorFault, 0UL, repeatMs);
  assert(alert.stateChanged && alert.alarmActive && alert.reminderDue);
  assert(tracker.reminderScheduled && tracker.lastReminder == 0UL);

  // An alarm at time zero must not be repeated on the next poll.
  alert = updateAlert(tracker, TempState::SensorFault, 2000UL, repeatMs);
  assert(!alert.stateChanged && alert.alarmActive && !alert.reminderDue);
  alert = updateAlert(tracker, TempState::SensorFault, 59999UL, repeatMs);
  assert(!alert.reminderDue);
  alert = updateAlert(tracker, TempState::SensorFault, 60000UL, repeatMs);
  assert(!alert.stateChanged && alert.reminderDue);
  assert(tracker.lastReminder == 60000UL);
  alert = updateAlert(tracker, TempState::SensorFault, 60001UL, repeatMs);
  assert(!alert.reminderDue);

  alert = updateAlert(tracker, TempState::Normal, 62000UL, repeatMs);
  assert(alert.stateChanged && !alert.alarmActive && !alert.reminderDue);
  assert(!tracker.reminderScheduled);
  alert = updateAlert(tracker, TempState::TooCold, 62001UL, repeatMs);
  assert(alert.stateChanged && alert.alarmActive && alert.reminderDue);
  assert(tracker.lastReminder == 62001UL);
  alert = updateAlert(tracker, TempState::TooHot, 64000UL, repeatMs);
  assert(alert.stateChanged && alert.reminderDue);
  alert = updateAlert(tracker, TempState::SensorFault, 65000UL, repeatMs);
  assert(alert.stateChanged && alert.reminderDue);
  alert = updateAlert(tracker, TempState::TooCold, 66000UL, repeatMs);
  assert(alert.stateChanged && alert.reminderDue);
  alert = updateAlert(tracker, TempState::TooCold, 125999UL, repeatMs);
  assert(!alert.stateChanged && !alert.reminderDue);
  alert = updateAlert(tracker, TempState::TooCold, 126000UL, repeatMs);
  assert(!alert.stateChanged && alert.reminderDue);
  alert = updateAlert(tracker, TempState::Normal, 126001UL, repeatMs);
  assert(alert.stateChanged && !alert.reminderDue && !tracker.reminderScheduled);
  alert = updateAlert(tracker, TempState::TooHot, 126002UL, repeatMs);
  assert(alert.stateChanged && alert.reminderDue);
  alert = updateAlert(tracker, TempState::TooHot, 186003UL, repeatMs);
  assert(!alert.stateChanged && alert.reminderDue);

  // Each abnormal-state transition resets the reminder on the firmware tracker.
  alert = updateAlert(tracker, TempState::TooCold, 186004UL, repeatMs);
  assert(alert.stateChanged && alert.alarmActive && alert.reminderDue);
  assert(tracker.previousState == TempState::TooCold && tracker.lastReminder == 186004UL);
  alert = updateAlert(tracker, TempState::SensorFault, 186005UL, repeatMs);
  assert(alert.stateChanged && alert.alarmActive && alert.reminderDue);
  assert(tracker.previousState == TempState::SensorFault && tracker.lastReminder == 186005UL);
  alert = updateAlert(tracker, TempState::TooHot, 186006UL, repeatMs);
  assert(alert.stateChanged && alert.alarmActive && alert.reminderDue);
  assert(tracker.previousState == TempState::TooHot && tracker.lastReminder == 186006UL);

  // Unsigned elapsed-time subtraction remains valid across millis() rollover.
  AlertTracker wrapTracker;
  const unsigned long nearWrap = std::numeric_limits<unsigned long>::max() - 100UL;
  alert = updateAlert(wrapTracker, TempState::TooHot, nearWrap, 150UL);
  assert(alert.reminderDue);
  alert = updateAlert(wrapTracker, TempState::TooHot, 48UL, 150UL);
  assert(!alert.reminderDue);
  alert = updateAlert(wrapTracker, TempState::TooHot, 49UL, 150UL);
  assert(alert.reminderDue);
  return 0;
}
