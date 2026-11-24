#include <cassert>
#include <cstdint>
#include <limits>
#include "sms_policy.h"

int main() {
  constexpr uint32_t fiveMinutes = 300000U;
  SmsPolicyTracker tracker;

  // Initial Normal is silent; first abnormal state generates an event.
  auto event = updateSmsPolicy(tracker, TempState::Normal, 0U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);
  event = updateSmsPolicy(tracker, TempState::TooCold, 2000U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalTransition && event.state == TempState::TooCold);
  event = updateSmsPolicy(tracker, TempState::TooCold, 301999U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);
  event = updateSmsPolicy(tracker, TempState::TooCold, 302000U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalReminder);
  event = updateSmsPolicy(tracker, TempState::TooCold, 304000U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);

  // Every directed transition between the three abnormal states is immediate.
  const TempState from[] = {TempState::TooCold, TempState::TooHot, TempState::SensorFault,
                            TempState::TooHot, TempState::SensorFault, TempState::TooCold};
  const TempState to[] = {TempState::TooHot, TempState::SensorFault, TempState::TooCold,
                          TempState::TooCold, TempState::TooHot, TempState::SensorFault};
  for (uint32_t i = 0; i < 6; ++i) {
    SmsPolicyTracker pairTracker;
    event = updateSmsPolicy(pairTracker, from[i], i * 100U, fiveMinutes);
    assert(event.kind == SmsEventKind::AbnormalTransition);
    event = updateSmsPolicy(pairTracker, to[i], i * 100U + 1U, fiveMinutes);
    assert(event.kind == SmsEventKind::AbnormalTransition && event.state == to[i]);
    event = updateSmsPolicy(pairTracker, to[i], i * 100U + 2U, fiveMinutes);
    assert(event.kind == SmsEventKind::None);
  }

  // Sensor fault, recovery once, and a new abnormal episode.
  event = updateSmsPolicy(tracker, TempState::SensorFault, 305000U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalTransition);
  event = updateSmsPolicy(tracker, TempState::Normal, 307000U, fiveMinutes);
  assert(event.kind == SmsEventKind::Recovery && event.state == TempState::Normal);
  event = updateSmsPolicy(tracker, TempState::Normal, 607000U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);
  event = updateSmsPolicy(tracker, TempState::TooHot, 609000U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalTransition);
  event = updateSmsPolicy(tracker, TempState::TooHot, 908999U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);
  event = updateSmsPolicy(tracker, TempState::TooHot, 909000U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalReminder);

  // Time zero and the physical 32-bit millis() rollover boundary.
  SmsPolicyTracker zeroTracker;
  event = updateSmsPolicy(zeroTracker, TempState::SensorFault, 0U, fiveMinutes);
  assert(event.kind == SmsEventKind::AbnormalTransition);
  event = updateSmsPolicy(zeroTracker, TempState::SensorFault, 2000U, fiveMinutes);
  assert(event.kind == SmsEventKind::None);
  SmsPolicyTracker wrapTracker;
  const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 100U;
  event = updateSmsPolicy(wrapTracker, TempState::TooHot, nearWrap, 150U);
  assert(event.kind == SmsEventKind::AbnormalTransition);
  event = updateSmsPolicy(wrapTracker, TempState::TooHot, 48U, 150U);
  assert(event.kind == SmsEventKind::None);
  event = updateSmsPolicy(wrapTracker, TempState::TooHot, 49U, 150U);
  assert(event.kind == SmsEventKind::AbnormalReminder);

  // Queue is bounded, keeps the freshest event, and discards stale entries.
  SmsOutbox outbox;
  SmsEvent queued;
  assert(!enqueueSmsEvent(outbox, {}, 0U));
  for (uint32_t i = 0; i < SMS_QUEUE_CAPACITY + 1U; ++i) {
    assert(enqueueSmsEvent(outbox,
        {SmsEventKind::AbnormalTransition, TempState::SensorFault, i}, i));
  }
  assert(outbox.count == SMS_QUEUE_CAPACITY && outbox.droppedFull == 1U);
  assert(takeNextSmsEvent(outbox, 10U, queued) && queued.occurredAtMs == 1U);
  assert(enqueueSmsEvent(outbox,
      {SmsEventKind::Recovery, TempState::Normal, 20U}, 20U));
  discardStaleSmsEvents(outbox, SMS_EVENT_MAX_AGE_MS + 20U);
  assert(outbox.count == 0U && outbox.droppedStale == SMS_QUEUE_CAPACITY);
  assert(!enqueueSmsEvent(outbox,
      {SmsEventKind::AbnormalReminder, TempState::TooHot, 0U}, SMS_EVENT_MAX_AGE_MS));

  SmsOutbox wrapOutbox;
  assert(enqueueSmsEvent(wrapOutbox,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, nearWrap}, nearWrap));
  assert(takeNextSmsEvent(wrapOutbox, 49U, queued) && queued.occurredAtMs == nearWrap);
  return 0;
}
