#pragma once

#include <stdint.h>
#include "logic.h"

// Policy events describe what should be reported. They are not SMS attempts or
// evidence that a modem accepted a message. VC-005 owns those later stages.
enum class SmsEventKind { None, AbnormalTransition, AbnormalReminder, Recovery };

struct SmsEvent {
  SmsEventKind kind = SmsEventKind::None;
  TempState state = TempState::Normal;
  uint32_t occurredAtMs = 0;
};

struct SmsPolicyTracker {
  TempState previousState = TempState::Normal;
  bool hasPrevious = false;
  uint32_t lastAbnormalEventAtMs = 0;
  bool reminderScheduled = false;
};

inline SmsEvent updateSmsPolicy(SmsPolicyTracker& tracker, TempState current,
                                uint32_t now, uint32_t repeatMs) {
  const bool changed = !tracker.hasPrevious || current != tracker.previousState;
  const bool recovered = tracker.hasPrevious &&
      tracker.previousState != TempState::Normal && current == TempState::Normal;
  tracker.previousState = current;
  tracker.hasPrevious = true;

  if (current == TempState::Normal) {
    tracker.reminderScheduled = false;
    return {recovered ? SmsEventKind::Recovery : SmsEventKind::None, current, now};
  }

  if (changed || !tracker.reminderScheduled) {
    tracker.lastAbnormalEventAtMs = now;
    tracker.reminderScheduled = true;
    return {SmsEventKind::AbnormalTransition, current, now};
  }

  if (uint32_t(now - tracker.lastAbnormalEventAtMs) >= repeatMs) {
    tracker.lastAbnormalEventAtMs = now;
    return {SmsEventKind::AbnormalReminder, current, now};
  }
  return {SmsEventKind::None, current, now};
}

// Fixed-size storage for a future transport. A full queue evicts the oldest
// event so the newest state is represented. Expired events are not sent later
// with potentially misleading old temperatures. Counters expose losses.
constexpr uint8_t SMS_QUEUE_CAPACITY = 8;
constexpr uint32_t SMS_EVENT_MAX_AGE_MS = 900000U;  // 15 minutes

struct SmsOutbox {
  SmsEvent entries[SMS_QUEUE_CAPACITY];
  uint8_t count = 0;
  uint32_t droppedFull = 0;
  uint32_t droppedStale = 0;
};

inline void removeOldestSmsEvent(SmsOutbox& outbox) {
  for (uint8_t i = 1; i < outbox.count; ++i) outbox.entries[i - 1] = outbox.entries[i];
  --outbox.count;
}

inline void discardStaleSmsEvents(SmsOutbox& outbox, uint32_t now) {
  while (outbox.count &&
         uint32_t(now - outbox.entries[0].occurredAtMs) >= SMS_EVENT_MAX_AGE_MS) {
    removeOldestSmsEvent(outbox);
    ++outbox.droppedStale;
  }
}

inline bool enqueueSmsEvent(SmsOutbox& outbox, SmsEvent event, uint32_t now) {
  if (event.kind == SmsEventKind::None) return false;
  discardStaleSmsEvents(outbox, now);
  if (uint32_t(now - event.occurredAtMs) >= SMS_EVENT_MAX_AGE_MS) {
    ++outbox.droppedStale;
    return false;
  }
  if (outbox.count == SMS_QUEUE_CAPACITY) {
    removeOldestSmsEvent(outbox);
    ++outbox.droppedFull;
  }
  outbox.entries[outbox.count++] = event;
  return true;
}

inline bool takeNextSmsEvent(SmsOutbox& outbox, uint32_t now, SmsEvent& event) {
  discardStaleSmsEvents(outbox, now);
  if (!outbox.count) return false;
  event = outbox.entries[0];
  removeOldestSmsEvent(outbox);
  return true;
}
