#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sim800_protocol.h"
#include "sms_policy.h"

enum class SmsOutcome {
  NOT_ATTEMPTED, IN_PROGRESS, SUBMITTED, REJECTED, AMBIGUOUS, DEFERRED, EXPIRED
};

struct SmsTransportSettings {
  uint32_t commandTimeoutMs = 5000U;
  uint32_t promptTimeoutMs = 10000U;
  uint32_t submitTimeoutMs = 65000U;
  uint32_t retryDelayMs = 30000U;
  uint32_t pollDelayMs = 5000U;
  uint32_t registrationCheckMs = 30000U;
};

class SmsUart {
 public:
  virtual ~SmsUart() = default;
  virtual void write(const char* data, size_t length) = 0;
  virtual void writeByte(uint8_t byte) = 0;
};

// Fixed-memory cooperative transport. Caller feeds all received bytes and calls
// tick often; neither operation waits for the modem. There is one owned active
// event in addition to VC-004's eight queued events.
class SmsTransport {
 public:
  enum class Phase { Disabled, Probe, Echo, Sim, Registration, TextMode,
                     Ready, Prompt, Submission, Backoff, Fault };

  explicit SmsTransport(const char* recipient,
                        SmsTransportSettings settings = {})
      : recipient_(recipient), settings_(settings) {
    phase_ = validRecipient(recipient) ? Phase::Probe : Phase::Disabled;
  }

  static bool validRecipient(const char* value) {
    if (!value) return false;
    size_t digits = 0;
    for (size_t i = 0; i < 17; ++i) {
      if (!value[i]) return digits >= 6 && digits <= 15;
      if (i == 0 && value[i] == '+') continue;
      if (value[i] < '0' || value[i] > '9') return false;
      ++digits;
    }
    return false;
  }

  Phase phase() const { return phase_; }
  bool enabled() const { return phase_ != Phase::Disabled; }
  bool ready() const { return phase_ == Phase::Ready && !awaitingFresh_; }
  const char* statusLabel() const {
    switch (phase_) {
      case Phase::Disabled: return "OFF";
      case Phase::Probe: case Phase::Echo: case Phase::Sim: return "INIT";
      case Phase::Registration: case Phase::TextMode: case Phase::Backoff: return "WAIT";
      case Phase::Ready: return awaitingFresh_ ? "WAIT" : "READY";
      case Phase::Prompt: case Phase::Submission: return "SEND";
      case Phase::Fault: return "FAIL";
    }
    return "WAIT";
  }
  uint8_t queued() const { return outbox_.count; }
  uint32_t droppedFull() const { return outbox_.droppedFull; }
  uint32_t droppedStale() const { return outbox_.droppedStale; }
  uint32_t droppedObsolete() const { return outbox_.droppedObsolete; }
  uint32_t parserOverflows() const { return parserOverflows_; }
  uint32_t reconnects() const { return reconnects_; }
  uint32_t textModeFailures() const { return textModeFailures_; }
  SmsOutcome lastOutcome() const { return lastOutcome_; }
  uint32_t lastOutcomeId() const { return lastOutcomeId_; }
  int lastCmsError() const { return lastCmsError_; }
  uint8_t activeAttempts() const { return active_ ? attempts_ : 0; }
  uint32_t activeEventId() const { return active_ ? activeEvent_.id : 0; }
  uint32_t totalAttempts() const { return totalAttempts_; }
  uint32_t lastAttemptEventId() const { return lastAttemptEventId_; }
  uint8_t lastAttemptNumber() const { return lastAttemptNumber_; }
  bool hasActive() const { return active_; }

  void onSample(TempState state, SmsEvent event, uint32_t now, SmsUart& io) {
    const bool changed = hasLive_ && live_ != state;
    if (state != TempState::Normal && (!hasLive_ || live_ == TempState::Normal)) ++episode_;
    live_ = state;
    hasLive_ = true;
    lastSampleAt_ = now;

    if (changed && active_ && activeEvent_.state != state && phase_ != Phase::Submission) {
      if (phase_ == Phase::Prompt) io.writeByte(0x1B);  // ESC, before payload only.
      finish(SmsOutcome::DEFERRED, now);
      if (phase_ == Phase::Prompt) backoff(now, Phase::Probe);
    }
    discardStaleSmsEvents(outbox_, now);
    discardObsoleteSmsEvents(outbox_, state, episode_,
                             lastSubmittedEpisode_ == episode_ &&
                             lastSubmittedState_ != TempState::Normal);
    if (event.kind != SmsEventKind::None && phase_ != Phase::Disabled) {
      event.id = nextId_++;
      event.episode = episode_;
      const bool ambiguousCooldown = live_ != TempState::Normal &&
          lastOutcome_ == SmsOutcome::AMBIGUOUS && ambiguousEpisode_ == episode_ &&
          ambiguousState_ == live_ && uint32_t(now - ambiguousAt_) < 300000U;
      const bool duplicateReminder = event.kind == SmsEventKind::AbnormalReminder &&
          hasEquivalent(state);
      if (!ambiguousCooldown && !duplicateReminder &&
          (event.kind != SmsEventKind::Recovery ||
          (lastSubmittedEpisode_ == episode_ &&
           lastSubmittedState_ != TempState::Normal &&
           !ambiguousRecoveryForCurrentEpisode()))) {
        enqueueSmsEvent(outbox_, event, now);
      }
    }
    if (phase_ == Phase::Ready && awaitingFresh_) {
      awaitingFresh_ = false;
      reconcile(now);
    } else if (phase_ == Phase::Ready && changed) {
      reconcile(now);
    }
  }

  void onByte(char byte, uint32_t now, SmsUart& io) {
    if (phase_ == Phase::Disabled || phase_ == Phase::Fault) return;
    const AtReply reply = parser_.feed(byte);
    if (reply.kind == AtReplyKind::None || reply.kind == AtReplyKind::Other) return;
    if (reply.kind == AtReplyKind::Overflow) { ++parserOverflows_; return; }
    if (phase_ == Phase::Ready && reply.kind == AtReplyKind::NotRegistered) {
      backoff(now, Phase::Registration);
      return;
    }
    if (!waiting_) return;
    if (reply.kind == AtReplyKind::CmsError || reply.kind == AtReplyKind::Error) {
      lastCmsError_ = reply.kind == AtReplyKind::CmsError ? reply.code : -1;
      const bool permanent = reply.kind == AtReplyKind::CmsError &&
          (reply.code == 304 || reply.code == 305 || reply.code == 330);
      if (phase_ == Phase::Prompt || phase_ == Phase::Submission) {
        rejectActive(now, permanent);
      } else {
        if (phase_ == Phase::TextMode) textModeFailures_++;
        backoff(now, Phase::Probe);
      }
      return;
    }
    switch (phase_) {
      case Phase::Probe:
        if (reply.kind == AtReplyKind::Ok) advance(Phase::Echo);
        break;
      case Phase::Echo:
        if (reply.kind == AtReplyKind::Ok) advance(Phase::Sim);
        break;
      case Phase::Sim:
        if (reply.kind == AtReplyKind::SimReady) simReady_ = true;
        if (reply.kind == AtReplyKind::SimNotReady) simReady_ = false;
        if (reply.kind == AtReplyKind::Ok) {
          if (simReady_) advance(Phase::Registration);
          else backoff(now, Phase::Sim);
        }
        break;
      case Phase::Registration:
        if (reply.kind == AtReplyKind::RegisteredHome ||
            reply.kind == AtReplyKind::RegisteredRoaming) registered_ = true;
        if (reply.kind == AtReplyKind::NotRegistered) registered_ = false;
        if (reply.kind == AtReplyKind::Ok) {
          if (registered_) { lastRegistrationAt_ = now; advance(Phase::TextMode); }
          else backoff(now, Phase::Registration);
        }
        break;
      case Phase::TextMode:
        if (reply.kind == AtReplyKind::Ok) {
          advance(Phase::Ready);
          awaitingFresh_ = true;
          ++reconnects_;
        }
        break;
      case Phase::Prompt:
        if (reply.kind == AtReplyKind::Prompt && active_) {
          const char* body = messageBody(activeEvent_);
          io.write(body, strlen(body));
          io.writeByte(0x1A);
          phase_ = Phase::Submission;
          since_ = now;
          cmgsSeen_ = false;
          parser_.reset();
        }
        break;
      case Phase::Submission:
        if (reply.kind == AtReplyKind::Cmgs) cmgsSeen_ = true;
        if (reply.kind == AtReplyKind::Ok && cmgsSeen_) {
          finish(SmsOutcome::SUBMITTED, now);
          advance(Phase::Ready);
          reconcile(now);
        }
        break;
      default: break;
    }
  }

  void tick(uint32_t now, SmsUart& io) {
    if (phase_ == Phase::Disabled) return;
    discardStaleSmsEvents(outbox_, now);
    if (phase_ == Phase::Fault) return;
    if (active_ && phase_ != Phase::Submission && phase_ != Phase::Prompt &&
        uint32_t(now - activeEvent_.occurredAtMs) >= SMS_EVENT_MAX_AGE_MS)
      finish(SmsOutcome::EXPIRED, now);
    if (phase_ == Phase::Backoff) {
      if (uint32_t(now - since_) < settings_.pollDelayMs) return;
      advance(resume_);
    }
    if (waiting_) {
      const uint32_t timeout = phase_ == Phase::Prompt ? settings_.promptTimeoutMs :
          phase_ == Phase::Submission ? settings_.submitTimeoutMs : settings_.commandTimeoutMs;
      if (uint32_t(now - since_) < timeout) return;
      if (phase_ == Phase::Prompt) {
        io.writeByte(0x1B);  // Cancel a prompt that may have arrived late.
        rejectActive(now, false);
      } else if (phase_ == Phase::Submission) {
        finish(SmsOutcome::AMBIGUOUS, now); // Ctrl-Z was already sent.
        backoff(now, Phase::Probe);
      } else backoff(now, Phase::Probe);
      return;
    }
    switch (phase_) {
      case Phase::Probe: send(io, "AT\r", now); break;
      case Phase::Echo: send(io, "ATE0\r", now); break;
      case Phase::Sim: simReady_ = false; send(io, "AT+CPIN?\r", now); break;
      case Phase::Registration:
        registered_ = false; send(io, "AT+CREG?\r", now); break;
      case Phase::TextMode: send(io, "AT+CMGF=1\r", now); break;
      case Phase::Ready:
        if (awaitingFresh_ || !hasLive_) return;
        if (uint32_t(now - lastRegistrationAt_) >= settings_.registrationCheckMs) {
          advance(Phase::Registration); return;
        }
        if (!active_) {
          if (!takeNextSmsEvent(outbox_, now, activeEvent_)) return;
          active_ = true;
          attempts_ = 0;
          lastOutcome_ = SmsOutcome::NOT_ATTEMPTED;
        }
        if (activeEvent_.state != live_ || activeEvent_.episode != episode_) {
          finish(SmsOutcome::DEFERRED, now); return;
        }
        if (attempts_ && uint32_t(now - lastAttemptAt_) < settings_.retryDelayMs) return;
        if (attempts_ >= 2) { finish(SmsOutcome::REJECTED, now); return; }
        sendCmgs(io, now);
        break;
      default: break;
    }
  }

 private:
  static const char* messageBody(const SmsEvent& event) {
    switch (event.state) {
      case TempState::TooHot: return "Vaccine carrier: HIGH TEMPERATURE. Check carrier.";
      case TempState::TooCold: return "Vaccine carrier: LOW TEMPERATURE. Check carrier.";
      case TempState::SensorFault: return "Vaccine carrier: SENSOR FAULT. Check sensor.";
      case TempState::Normal: return "Vaccine carrier: temperature returned to NORMAL.";
    }
    return "Vaccine carrier: status unknown.";
  }
  void send(SmsUart& io, const char* command, uint32_t now) {
    parser_.reset();
    io.write(command, strlen(command));
    waiting_ = true;
    since_ = now;
  }
  void sendCmgs(SmsUart& io, uint32_t now) {
    char command[48];
    const int length = snprintf(command, sizeof(command), "AT+CMGS=\"%s\"\r", recipient_);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(command)) {
      finish(SmsOutcome::REJECTED, now); return;
    }
    ++attempts_;
    ++totalAttempts_;
    lastAttemptEventId_ = activeEvent_.id;
    lastAttemptNumber_ = attempts_;
    lastAttemptAt_ = now;
    lastOutcome_ = SmsOutcome::IN_PROGRESS;
    lastCmsError_ = -1;
    phase_ = Phase::Prompt;
    send(io, command, now);
  }
  void advance(Phase next) {
    phase_ = next;
    waiting_ = false;
    parser_.reset();
  }
  void backoff(uint32_t now, Phase resume) {
    resume_ = resume;
    phase_ = Phase::Backoff;
    waiting_ = false;
    since_ = now;
    parser_.reset();
  }
  void rejectActive(uint32_t now, bool permanent) {
    lastOutcome_ = SmsOutcome::REJECTED;
    lastOutcomeId_ = activeEvent_.id;
    if (permanent || attempts_ >= 2) finish(SmsOutcome::REJECTED, now);
    if (permanent) {
      phase_ = Phase::Fault; // Requires operator/configuration intervention.
      waiting_ = false;
      parser_.reset();
    } else backoff(now, Phase::Probe);
  }
  void finish(SmsOutcome outcome, uint32_t now) {
    if (!active_) return;
    lastOutcome_ = outcome;
    lastOutcomeId_ = activeEvent_.id;
    if (outcome == SmsOutcome::SUBMITTED) {
      lastSubmittedEpisode_ = activeEvent_.episode;
      lastSubmittedState_ = activeEvent_.state;
      lastSubmittedAt_ = now;
      // An acknowledgement for this episode/state supersedes equivalent
      // queued events that accumulated during the active attempt.
      uint8_t keep = 0;
      for (uint8_t i = 0; i < outbox_.count; ++i) {
        const SmsEvent& queued = outbox_.entries[i];
        if (queued.episode == activeEvent_.episode && queued.state == activeEvent_.state)
          ++outbox_.droppedObsolete;
        else outbox_.entries[keep++] = queued;
      }
      outbox_.count = keep;
    } else if (outcome == SmsOutcome::AMBIGUOUS) {
      ambiguousEpisode_ = activeEvent_.episode;
      ambiguousKind_ = activeEvent_.kind;
      ambiguousState_ = activeEvent_.state;
      ambiguousAt_ = now;
    } else if (outcome == SmsOutcome::REJECTED) {
      rejectedEpisode_ = activeEvent_.episode;
      rejectedState_ = activeEvent_.state;
      rejectedAt_ = now;
    }
    active_ = false;
    attempts_ = 0;
  }
  bool hasEquivalent(TempState state) const {
    if (active_ && activeEvent_.state == state && activeEvent_.episode == episode_) return true;
    for (uint8_t i = 0; i < outbox_.count; ++i)
      if (outbox_.entries[i].state == state && outbox_.entries[i].episode == episode_) return true;
    return false;
  }
  bool ambiguousRecoveryForCurrentEpisode() const {
    return ambiguousKind_ == SmsEventKind::Recovery && ambiguousEpisode_ == episode_;
  }
  void reconcile(uint32_t now) {
    if (phase_ != Phase::Ready || awaitingFresh_ || !hasLive_) return;
    discardObsoleteSmsEvents(outbox_, live_, episode_,
                             lastSubmittedEpisode_ == episode_ &&
                             lastSubmittedState_ != TempState::Normal);
    if (hasEquivalent(live_)) return;
    if (live_ == TempState::Normal) {
      if (lastSubmittedEpisode_ == episode_ &&
          lastSubmittedState_ != TempState::Normal &&
          !ambiguousRecoveryForCurrentEpisode())
        enqueueSmsEvent(outbox_, {SmsEventKind::Recovery, live_, now, nextId_++, episode_}, now);
      return;
    }
    if (lastSubmittedEpisode_ == episode_ && lastSubmittedState_ == live_ &&
        uint32_t(now - lastSubmittedAt_) < 300000U) return;
    if (lastOutcome_ == SmsOutcome::REJECTED &&
        rejectedEpisode_ == episode_ && rejectedState_ == live_ &&
        uint32_t(now - rejectedAt_) < 300000U) return;
    // A post-payload timeout may already have produced a text. Wait for a
    // later policy reminder rather than immediately synthesizing a duplicate.
    if (lastOutcome_ == SmsOutcome::AMBIGUOUS &&
        ambiguousEpisode_ == episode_ && ambiguousState_ == live_ &&
        uint32_t(now - ambiguousAt_) < 300000U) return;
    enqueueSmsEvent(outbox_, {SmsEventKind::AbnormalTransition, live_, now,
                             nextId_++, episode_}, now);
  }

  const char* recipient_;
  SmsTransportSettings settings_;
  Sim800Parser parser_;
  SmsOutbox outbox_;
  Phase phase_ = Phase::Disabled;
  Phase resume_ = Phase::Probe;
  bool waiting_ = false, simReady_ = false, registered_ = false;
  bool cmgsSeen_ = false, awaitingFresh_ = true;
  bool active_ = false, hasLive_ = false;
  TempState live_ = TempState::Normal;
  SmsEvent activeEvent_;
  uint32_t since_ = 0, lastRegistrationAt_ = 0, lastAttemptAt_ = 0;
  uint32_t lastSampleAt_ = 0, nextId_ = 1, episode_ = 0;
  uint32_t lastSubmittedEpisode_ = 0, lastSubmittedAt_ = 0;
  TempState lastSubmittedState_ = TempState::Normal;
  uint32_t ambiguousEpisode_ = 0, ambiguousAt_ = 0;
  SmsEventKind ambiguousKind_ = SmsEventKind::None;
  TempState ambiguousState_ = TempState::Normal;
  uint32_t rejectedEpisode_ = 0, rejectedAt_ = 0;
  TempState rejectedState_ = TempState::Normal;
  uint32_t parserOverflows_ = 0, reconnects_ = 0, textModeFailures_ = 0;
  uint32_t lastOutcomeId_ = 0;
  uint32_t totalAttempts_ = 0, lastAttemptEventId_ = 0;
  uint8_t lastAttemptNumber_ = 0;
  uint8_t attempts_ = 0;
  int lastCmsError_ = -1;
  SmsOutcome lastOutcome_ = SmsOutcome::NOT_ATTEMPTED;
};
