#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include "sms_transport.h"

struct FakeUart : SmsUart {
  std::string sent;
  void write(const char* data, size_t length) override { sent.append(data, length); }
  void writeByte(uint8_t byte) override { sent.push_back(static_cast<char>(byte)); }
  void clear() { sent.clear(); }
};

static void feed(SmsTransport& modem, FakeUart& io, const char* reply, uint32_t now) {
  for (const char* p = reply; *p; ++p) modem.onByte(*p, now, io);
}

static void initialize(SmsTransport& modem, FakeUart& io, uint32_t now = 0,
                       TempState sample = TempState::Normal) {
  modem.tick(now, io); assert(io.sent == "AT\r"); io.clear();
  feed(modem, io, "\r\nOK\r\n", now);
  modem.tick(now, io); assert(io.sent == "ATE0\r"); io.clear();
  feed(modem, io, "ATE0\r\nOK\r\n", now);
  modem.tick(now, io); assert(io.sent == "AT+CPIN?\r"); io.clear();
  feed(modem, io, "+CPIN: READY\r\nOK\r\n", now);
  modem.tick(now, io); assert(io.sent == "AT+CREG?\r"); io.clear();
  feed(modem, io, "+CREG: 0,1\r\nOK\r\n", now);
  modem.tick(now, io); assert(io.sent == "AT+CMGF=1\r"); io.clear();
  feed(modem, io, "OK\r\n", now);
  assert(modem.phase() == SmsTransport::Phase::Ready && !modem.ready());
  modem.onSample(sample, {}, now, io);
  assert(modem.ready());
}

static void submitPrompt(SmsTransport& modem, FakeUart& io, uint32_t now) {
  modem.tick(now, io);
  assert(io.sent.find("AT+CMGS=\"+12025550123\"\r") != std::string::npos);
  io.clear();
  feed(modem, io, "\r\n> ", now);
  assert(io.sent.find("Vaccine carrier:") != std::string::npos);
  assert(!io.sent.empty() && io.sent.back() == 0x1A);
  io.clear();
}

int main() {
  // Recipient validation prevents AT command injection and disabled mode does
  // not accumulate events.
  assert(SmsTransport::validRecipient("+12025550123"));
  assert(!SmsTransport::validRecipient("+123\"\rAT"));
  assert(!SmsTransport::validRecipient(""));
  assert(!SmsTransport::validRecipient("1234567890123456"));
  FakeUart io;
  SmsTransport disabled("");
  disabled.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  disabled.tick(0, io);
  assert(!disabled.enabled() && disabled.queued() == 0 && io.sent.empty());

  // Partial and multiple replies, unsolicited lines, malformed registrations,
  // and oversized lines cannot produce spurious acknowledgements.
  Sim800Parser parser;
  assert(parser.feed('+').kind == AtReplyKind::None);
  assert(parser.feed('C').kind == AtReplyKind::None);
  for (char c : std::string("REG: 0,5\r\n")) {
    auto reply = parser.feed(c);
    if (c == '\n') assert(reply.kind == AtReplyKind::RegisteredRoaming);
  }
  for (char c : std::string("+CREG: nonsense\r\n")) {
    auto reply = parser.feed(c);
    if (c == '\n') assert(reply.kind == AtReplyKind::Other);
  }
  for (size_t i = 0; i < Sim800Parser::CAPACITY + 5; ++i) parser.feed('X');
  assert(parser.feed('\n').kind == AtReplyKind::Overflow);
  assert(parser.feed('>').kind == AtReplyKind::Prompt);
  parser.feed(' ');
  for (char c : std::string("+CMS ERROR: 304\r\n")) {
    auto reply = parser.feed(c);
    if (c == '\n') assert(reply.kind == AtReplyKind::CmsError && reply.code == 304);
  }
  for (char c : std::string("+CMGS: nonsense\r\n")) {
    auto reply = parser.feed(c);
    if (c == '\n') assert(reply.kind == AtReplyKind::Other);
  }
  for (char c : std::string("RING\r\nOK\r\n")) {
    auto reply = parser.feed(c);
    if (c == '\n' && reply.kind == AtReplyKind::Other) continue;
  }

  // A silent modem times out and enters bounded backoff.
  SmsTransport silent("+12025550123");
  silent.tick(0, io); assert(io.sent == "AT\r"); io.clear();
  silent.tick(4999, io); assert(io.sent.empty());
  silent.tick(5000, io); assert(silent.phase() == SmsTransport::Phase::Backoff);
  silent.tick(9999, io); assert(io.sent.empty());
  silent.tick(10000, io); assert(io.sent == "AT\r"); io.clear();

  // SIM not ready and delayed registration do not advance to SMS setup.
  SmsTransport waiting("+12025550123");
  waiting.tick(0, io); io.clear(); feed(waiting, io, "OK\r\n", 0);
  waiting.tick(0, io); io.clear(); feed(waiting, io, "OK\r\n", 0);
  waiting.tick(0, io); io.clear(); feed(waiting, io, "+CPIN: SIM PIN\r\nOK\r\n", 0);
  assert(waiting.phase() == SmsTransport::Phase::Backoff);
  waiting.tick(5000, io); assert(io.sent == "AT+CPIN?\r"); io.clear();
  feed(waiting, io, "+CPIN: READY\r\nOK\r\n", 5000);
  waiting.tick(5000, io); io.clear();
  feed(waiting, io, "+CREG: 0,2\r\nOK\r\n", 5000);
  assert(waiting.phase() == SmsTransport::Phase::Backoff);
  waiting.tick(10000, io); assert(io.sent == "AT+CREG?\r"); io.clear();
  feed(waiting, io, "+CREG: 0,5\r\nOK\r\n", 10000);
  waiting.tick(10000, io); assert(io.sent == "AT+CMGF=1\r"); io.clear();
  feed(waiting, io, "ERROR\r\n", 10000);
  assert(waiting.phase() == SmsTransport::Phase::Backoff && waiting.textModeFailures() == 1);

  // Positively acknowledged submission needs both +CMGS and final OK.
  SmsTransport success("+12025550123");
  initialize(success, io);
  success.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 10}, 10, io);
  assert(success.queued() == 1);
  submitPrompt(success, io, 10);
  feed(success, io, "+CMGS: 12\r\n", 20);
  assert(success.lastOutcome() != SmsOutcome::SUBMITTED);
  feed(success, io, "OK\r\n", 20);
  assert(success.lastOutcome() == SmsOutcome::SUBMITTED && !success.hasActive());
  assert(success.queued() == 0);
  feed(success, io, "+CREG: 0,0\r\n", 30);
  assert(success.phase() == SmsTransport::Phase::Backoff);
  success.onSample(TempState::Normal,
      {SmsEventKind::Recovery, TempState::Normal, 40}, 40, io);
  assert(success.queued() == 1);
  success.tick(5030, io); assert(io.sent == "AT+CREG?\r"); io.clear();
  feed(success, io, "+CREG: 0,1\r\nOK\r\n", 5030);
  success.tick(5030, io); assert(io.sent == "AT+CMGF=1\r"); io.clear();
  feed(success, io, "OK\r\n", 5030);
  success.onSample(TempState::Normal, {}, 5031, io);
  assert(success.queued() == 1); // Exactly one justified recovery.

  // Explicit permanent error does not retry. A transient rejection can retry
  // only after resynchronization and at least 30 seconds.
  SmsTransport permanent("+12025550123");
  initialize(permanent, io);
  permanent.onSample(TempState::SensorFault,
      {SmsEventKind::AbnormalTransition, TempState::SensorFault, 0}, 0, io);
  submitPrompt(permanent, io, 0);
  feed(permanent, io, "+CMS ERROR: 304\r\n", 1);
  assert(!permanent.hasActive() && permanent.lastOutcome() == SmsOutcome::REJECTED);
  assert(permanent.phase() == SmsTransport::Phase::Fault);
  permanent.tick(300000, io);
  assert(permanent.totalAttempts() == 1 && io.sent.empty());

  SmsTransport retry("+12025550123");
  initialize(retry, io);
  retry.onSample(TempState::TooCold,
      {SmsEventKind::AbnormalTransition, TempState::TooCold, 0}, 0, io);
  retry.tick(0, io); io.clear();
  feed(retry, io, "+CMS ERROR: 500\r\n", 1);
  assert(retry.hasActive() && retry.activeAttempts() == 1);
  retry.tick(5001, io); io.clear(); feed(retry, io, "OK\r\n", 5001);
  retry.tick(5001, io); io.clear(); feed(retry, io, "OK\r\n", 5001);
  retry.tick(5001, io); io.clear(); feed(retry, io, "+CPIN: READY\r\nOK\r\n", 5001);
  retry.tick(5001, io); io.clear(); feed(retry, io, "+CREG: 0,1\r\nOK\r\n", 5001);
  retry.tick(5001, io); io.clear(); feed(retry, io, "OK\r\n", 5001);
  retry.onSample(TempState::TooCold, {}, 5001, io);
  retry.tick(29999, io); assert(io.sent.empty());
  retry.tick(30000, io); assert(retry.activeAttempts() == 2); io.clear();
  feed(retry, io, "ERROR\r\n", 30001);
  assert(!retry.hasActive() && retry.lastOutcome() == SmsOutcome::REJECTED);

  // CMS 331 means network unavailable. Reprobe at bounded intervals, wait for
  // registration, then make only the second eligible attempt after 30 seconds.
  FakeUart networkIo;
  SmsTransport network("+12025550123");
  initialize(network, networkIo);
  network.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, networkIo);
  submitPrompt(network, networkIo, 0);
  feed(network, networkIo, "+CMS ERROR: 331\r\n", 1);
  assert(network.phase() == SmsTransport::Phase::Backoff);
  assert(network.hasActive() && network.activeAttempts() == 1);
  network.tick(5000, networkIo); assert(networkIo.sent.empty());
  network.tick(5001, networkIo); assert(networkIo.sent == "AT\r"); networkIo.clear();
  feed(network, networkIo, "OK\r\n", 5001);
  network.tick(5001, networkIo); assert(networkIo.sent == "ATE0\r"); networkIo.clear();
  feed(network, networkIo, "OK\r\n", 5001);
  network.tick(5001, networkIo); assert(networkIo.sent == "AT+CPIN?\r"); networkIo.clear();
  feed(network, networkIo, "+CPIN: READY\r\nOK\r\n", 5001);
  network.tick(5001, networkIo); assert(networkIo.sent == "AT+CREG?\r"); networkIo.clear();
  feed(network, networkIo, "+CREG: 0,2\r\nOK\r\n", 5001);
  assert(network.phase() == SmsTransport::Phase::Backoff);
  network.tick(10000, networkIo); assert(networkIo.sent.empty());
  network.tick(10001, networkIo); assert(networkIo.sent == "AT+CREG?\r"); networkIo.clear();
  feed(network, networkIo, "+CREG: 0,1\r\nOK\r\n", 10001);
  network.tick(10001, networkIo); assert(networkIo.sent == "AT+CMGF=1\r"); networkIo.clear();
  feed(network, networkIo, "OK\r\n", 10001);
  network.onSample(TempState::TooHot, {}, 10002, networkIo);
  network.tick(29999, networkIo); assert(networkIo.sent.empty());
  submitPrompt(network, networkIo, 30000);
  assert(network.activeAttempts() == 2);
  feed(network, networkIo, "+CMGS: 9\r\nOK\r\n", 30001);
  assert(network.lastOutcome() == SmsOutcome::SUBMITTED && !network.hasActive());
  network.tick(30002, networkIo);
  assert(network.totalAttempts() == 2 && networkIo.sent.empty());

  // A pre-payload prompt timeout is cancelled and eligible for bounded retry.
  SmsTransport promptTimeout("+12025550123");
  initialize(promptTimeout, io);
  promptTimeout.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  promptTimeout.tick(0, io); io.clear();
  promptTimeout.tick(10000, io);
  assert(promptTimeout.hasActive() && promptTimeout.activeAttempts() == 1);
  assert(io.sent == std::string(1, 0x1B)); io.clear();

  // A post-payload timeout is ambiguous and is never resent as that event.
  SmsTransport ambiguous("+12025550123");
  initialize(ambiguous, io);
  ambiguous.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  submitPrompt(ambiguous, io, 0);
  ambiguous.tick(64999, io); assert(ambiguous.hasActive());
  ambiguous.tick(65000, io);
  assert(!ambiguous.hasActive() && ambiguous.lastOutcome() == SmsOutcome::AMBIGUOUS);
  assert(ambiguous.queued() == 0);
  initialize(ambiguous, io, 70000, TempState::TooHot);
  assert(ambiguous.queued() == 0); // No immediate duplicate after ambiguous result.

  // A recovery payload may have been accepted even when its final response is
  // lost. Reconnection must not synthesize another recovery for that episode.
  FakeUart recoveryIo;
  SmsTransport recovery("+12025550123");
  initialize(recovery, recoveryIo);
  recovery.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, recoveryIo);
  submitPrompt(recovery, recoveryIo, 0);
  feed(recovery, recoveryIo, "+CMGS: 10\r\nOK\r\n", 1);
  assert(recovery.lastOutcome() == SmsOutcome::SUBMITTED);
  recovery.onSample(TempState::Normal,
      {SmsEventKind::Recovery, TempState::Normal, 2}, 2, recoveryIo);
  submitPrompt(recovery, recoveryIo, 2);
  recovery.tick(65002, recoveryIo);
  assert(recovery.lastOutcome() == SmsOutcome::AMBIGUOUS);
  assert(!recovery.hasActive() && recovery.queued() == 0);
  const uint32_t attemptsBeforeReconnect = recovery.totalAttempts();
  initialize(recovery, recoveryIo, 70002, TempState::Normal);
  recovery.tick(70003, recoveryIo);
  assert(recovery.lastOutcome() == SmsOutcome::AMBIGUOUS);
  assert(recovery.queued() == 0 && recoveryIo.sent.empty());
  assert(recovery.totalAttempts() == attemptsBeforeReconnect);
  recovery.onSample(TempState::Normal, {}, 72000, recoveryIo);
  assert(recovery.queued() == 0);

  // A distinct abnormal episode retains its own legitimate recovery event.
  recovery.onSample(TempState::TooCold,
      {SmsEventKind::AbnormalTransition, TempState::TooCold, 73000}, 73000, recoveryIo);
  submitPrompt(recovery, recoveryIo, 73000);
  feed(recovery, recoveryIo, "+CMGS: 11\r\nOK\r\n", 73001);
  recovery.onSample(TempState::Normal,
      {SmsEventKind::Recovery, TempState::Normal, 73002}, 73002, recoveryIo);
  assert(recovery.queued() == 1);
  submitPrompt(recovery, recoveryIo, 73002);
  feed(recovery, recoveryIo, "+CMGS: 12\r\nOK\r\n", 73003);
  assert(recovery.lastOutcome() == SmsOutcome::SUBMITTED && recovery.queued() == 0);

  // A live state change before payload cancels the old request. A change after
  // payload waits for its outcome, then queues only a justified recovery.
  SmsTransport beforePayload("+12025550123");
  initialize(beforePayload, io);
  beforePayload.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  beforePayload.tick(0, io); io.clear();
  beforePayload.onSample(TempState::TooCold,
      {SmsEventKind::AbnormalTransition, TempState::TooCold, 1}, 1, io);
  assert(io.sent == std::string(1, 0x1B)); io.clear();
  assert(!beforePayload.hasActive() && beforePayload.queued() == 1);
  assert(beforePayload.lastOutcome() == SmsOutcome::DEFERRED);

  SmsTransport afterPayload("+12025550123");
  initialize(afterPayload, io);
  afterPayload.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  submitPrompt(afterPayload, io, 0);
  afterPayload.onSample(TempState::Normal,
      {SmsEventKind::Recovery, TempState::Normal, 1}, 1, io);
  assert(afterPayload.hasActive() && afterPayload.queued() == 0);
  feed(afterPayload, io, "+CMGS: 7\r\nOK\r\n", 2);
  assert(afterPayload.lastOutcome() == SmsOutcome::SUBMITTED);
  assert(afterPayload.queued() == 1); // One recovery, not the stale hot warning.
  afterPayload.onSample(TempState::Normal, {}, 3, io);
  assert(afterPayload.queued() == 1);

  SmsTransport sameAgain("+12025550123");
  initialize(sameAgain, io);
  sameAgain.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  submitPrompt(sameAgain, io, 0);
  sameAgain.onSample(TempState::TooCold,
      {SmsEventKind::AbnormalTransition, TempState::TooCold, 1}, 1, io);
  sameAgain.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 2}, 2, io);
  assert(sameAgain.queued() == 1);
  feed(sameAgain, io, "+CMGS: 8\r\nOK\r\n", 3);
  assert(sameAgain.queued() == 0); // Active acknowledgement covers current state.

  // Reconnection with an expired historical event generates a current-state
  // event for each abnormal category after a fresh sample.
  for (TempState state : {TempState::TooHot, TempState::TooCold,
                          TempState::SensorFault}) {
    FakeUart local;
    SmsTransport recovered("+12025550123");
    recovered.onSample(state, {SmsEventKind::AbnormalTransition, state, 0}, 0, local);
    initialize(recovered, local, SMS_EVENT_MAX_AGE_MS, state);
    assert(recovered.droppedStale() == 1);
    assert(recovered.queued() == 1);
    recovered.onSample(state, {}, SMS_EVENT_MAX_AGE_MS + 1, local);
    assert(recovered.queued() == 1);
    recovered.onSample(state, {}, SMS_EVENT_MAX_AGE_MS + 2, local);
    assert(recovered.queued() == 1);
  }

  // The policy keeps monitoring and producing events while GSM is absent.
  SmsTransport offline("+12025550123");
  SmsPolicyTracker policy;
  FakeUart offlineIo;
  for (uint32_t i = 0; i < 4; ++i) {
    const TempState state = i & 1 ? TempState::TooCold : TempState::TooHot;
    const SmsEvent event = updateSmsPolicy(policy, state, i * 2000U, 300000U);
    offline.onSample(state, event, i * 2000U, offlineIo);
    offline.tick(i * 2000U, offlineIo);
    assert(event.kind == SmsEventKind::AbnormalTransition);
  }
  assert(offline.queued() == 1); // Current state, not four stale warnings.
  offline.onSample(TempState::TooCold,
      {SmsEventKind::AbnormalReminder, TempState::TooCold, 300000}, 300000, offlineIo);
  assert(offline.queued() == 1); // Reminder coalesces with pending same state.

  // VC-004 queue bounds, expiry, 32-bit rollover, and live-state suppression.
  SmsOutbox queue;
  for (uint32_t i = 0; i <= SMS_QUEUE_CAPACITY; ++i)
    enqueueSmsEvent(queue, {SmsEventKind::AbnormalReminder, TempState::TooHot, i}, i);
  assert(queue.count == 8 && queue.droppedFull == 1);
  discardStaleSmsEvents(queue, SMS_EVENT_MAX_AGE_MS + 20);
  assert(queue.count == 0 && queue.droppedStale == 8);
  SmsTransport wrap("+12025550123"); FakeUart wrapIo;
  const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 100U;
  wrap.tick(nearWrap, wrapIo); wrapIo.clear();
  wrap.tick(4898U, wrapIo); assert(wrapIo.sent.empty());
  wrap.tick(4899U, wrapIo); assert(wrap.phase() == SmsTransport::Phase::Backoff);

  SmsTransport stale("+12025550123");
  stale.onSample(TempState::TooHot,
      {SmsEventKind::AbnormalTransition, TempState::TooHot, 0}, 0, io);
  stale.onSample(TempState::Normal,
      {SmsEventKind::Recovery, TempState::Normal, 1}, 1, io);
  assert(stale.queued() == 0); // No previously submitted abnormal episode.
  return 0;
}
