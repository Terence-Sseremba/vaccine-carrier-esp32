#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Pure C++ logging primitives. No hardware, clock, allocation, or policy decisions.
namespace logging {
constexpr uint32_t DEFAULT_INTERVAL_MS = 30000U;
constexpr size_t BOOT_CAPACITY = 40;
constexpr size_t DETAILS_CAPACITY = 128;
enum class Status { Normal, Low, High, SensorError };
enum class Event {
  Boot, TemperatureHigh, TemperatureLow, TemperatureRecovered, SensorError,
  SensorRecovered, AlertReminder, SmsPolicyEvent, SmsAttempt, SmsSubmitted,
  SmsFailed, SmsQueueLoss, StorageFailure, StorageRecovered, LogRecordLoss
};
inline const char* eventName(Event e) {
  switch (e) {
    case Event::Boot: return "BOOT";
    case Event::TemperatureHigh: return "TEMPERATURE_HIGH";
    case Event::TemperatureLow: return "TEMPERATURE_LOW";
    case Event::TemperatureRecovered: return "TEMPERATURE_RECOVERED";
    case Event::SensorError: return "SENSOR_ERROR";
    case Event::SensorRecovered: return "SENSOR_RECOVERED";
    case Event::AlertReminder: return "ALERT_REMINDER";
    case Event::SmsPolicyEvent: return "SMS_POLICY_EVENT";
    case Event::SmsAttempt: return "SMS_ATTEMPT";
    case Event::SmsSubmitted: return "SMS_SUBMITTED"; // Modem acceptance only.
    case Event::SmsFailed: return "SMS_FAILED";
    case Event::SmsQueueLoss: return "SMS_QUEUE_LOSS";
    case Event::StorageFailure: return "STORAGE_FAILURE";
    case Event::StorageRecovered: return "STORAGE_RECOVERED";
    case Event::LogRecordLoss: return "LOG_RECORD_LOSS";
  }
  return nullptr;
}
inline const char* statusName(Status s) {
  switch (s) {
    case Status::Normal: return "NORMAL";
    case Status::Low: return "LOW";
    case Status::High: return "HIGH";
    case Status::SensorError: return "SENSOR_ERROR";
  }
  return nullptr;
}
enum class Kind { Temperature, Event };
struct Record {
  char bootId[BOOT_CAPACITY] = {};
  uint64_t sequence = 0;
  uint32_t uptimeMs = 0;
  Kind kind = Kind::Temperature;
  float temperatureC = 0;
  Status status = Status::SensorError;
  bool valid = false;
  Event event = Event::Boot;
  char details[DETAILS_CAPACITY] = {};
  bool important() const { return kind == Kind::Event || !valid; }
};
inline bool copyText(char* out, size_t capacity, const char* in) {
  if (!in) return false;
  size_t n = 0;
  while (n < capacity && in[n]) ++n;
  if (n == capacity) return false;
  std::memcpy(out, in, n + 1);
  return true;
}
class Factory {
 public:
  explicit Factory(const char* bootId) {
    ready_ = copyText(boot_, sizeof boot_, bootId) && boot_[0];
  }
  bool ready() const { return ready_; }
  bool temperature(Record& out, uint32_t now, float value, Status status, bool valid) {
    if (!statusName(status)) return false;
    Record r;
    r.kind = Kind::Temperature;
    r.valid = valid && status != Status::SensorError && std::isfinite(value);
    r.status = r.valid ? status : Status::SensorError;
    r.temperatureC = r.valid ? value : 0;
    return finish(out, r, now);
  }
  bool event(Record& out, uint32_t now, Event event, const char* details = "") {
    if (!eventName(event)) return false;
    Record r;
    r.kind = Kind::Event;
    r.event = event;
    if (!copyText(r.details, sizeof r.details, details)) return false;
    return finish(out, r, now);
  }
 private:
  bool finish(Record& out, Record& r, uint32_t now) {
    if (!ready_ || exhausted_) return false;
    std::memcpy(r.bootId, boot_, sizeof boot_);
    r.uptimeMs = now;
    r.sequence = next_;
    if (next_ == UINT64_MAX) exhausted_ = true;
    else ++next_;
    out = r;
    return true;
  }
  char boot_[BOOT_CAPACITY] = {};
  uint64_t next_ = 1;
  bool ready_ = false, exhausted_ = false;
};
// Invoke on completed samples. First sample is due; no catch-up burst after gaps.
// Unsigned subtraction requires elapsed time since the last due anchor <2^32 ms.
class Schedule {
 public:
  explicit Schedule(uint32_t interval = DEFAULT_INTERVAL_MS) : interval_(interval) {}
  bool due(uint32_t now) {
    if (!started_ || uint32_t(now - previous_) >= interval_) {
      started_ = true; previous_ = now; return true;
    }
    return false;
  }
 private:
  uint32_t interval_, previous_ = 0;
  bool started_ = false;
};

constexpr const char* TEMPERATURE_HEADER = "boot_id,sequence,uptime_ms,temperature_c,status,valid\r\n";
constexpr const char* EVENT_HEADER = "boot_id,sequence,uptime_ms,event,details\r\n";
enum class FormatResult { Ok, BufferTooSmall, InvalidRecord };
// On failure output is empty when capacity > 0; required includes NUL.
inline FormatResult formatCsv(const Record& r, char* out, size_t capacity, size_t& required) {
  required = 0;
  if (out && capacity) out[0] = 0;
  if (!std::memchr(r.bootId, 0, sizeof r.bootId) || !r.bootId[0] ||
      !std::memchr(r.details, 0, sizeof r.details)) return FormatResult::InvalidRecord;
  const char* label = r.kind == Kind::Temperature ? statusName(r.status) : eventName(r.event);
  if (!label || (r.kind != Kind::Temperature && r.kind != Kind::Event)) return FormatResult::InvalidRecord;
  if (r.kind == Kind::Temperature && (r.valid != (r.status != Status::SensorError) ||
      (r.valid && (!std::isfinite(r.temperatureC) || std::fabs(r.temperatureC) > 1000000))))
    return FormatResult::InvalidRecord;
  // Bounded scratch buffer avoids exposing a partially formatted row on failure.
  char row[512]; size_t n = 0;
  auto put = [&](char c) { if (n < sizeof row) row[n] = c; ++n; };
  auto text = [&](const char* s) { while (*s) put(*s++); };
  auto field = [&](const char* s) {
    put('"'); while (*s) { if (*s == '"') put('"'); put(*s++); } put('"');
  };
  field(r.bootId); put(',');
  char number[48];
  int count = std::snprintf(number, sizeof number, "%llu,%lu,",
      static_cast<unsigned long long>(r.sequence), static_cast<unsigned long>(r.uptimeMs));
  if (count < 0 || static_cast<size_t>(count) >= sizeof number) return FormatResult::InvalidRecord;
  text(number);
  if (r.kind == Kind::Temperature) {
    if (r.valid) {
      const long long cents = static_cast<long long>(std::round(static_cast<double>(r.temperatureC) * 100));
      const unsigned long long magnitude = static_cast<unsigned long long>(cents < 0 ? -cents : cents);
      count = std::snprintf(number, sizeof number, "%s%llu.%02llu", cents < 0 ? "-" : "", magnitude / 100, magnitude % 100);
      if (count < 0 || static_cast<size_t>(count) >= sizeof number) return FormatResult::InvalidRecord;
      text(number);
    }
    put(','); text(label); text(r.valid ? ",1" : ",0");
  } else { text(label); put(','); field(r.details); }
  text("\r\n"); put(0);
  required = n;
  if (n > sizeof row) return FormatResult::InvalidRecord;
  if (!out || capacity < n) return FormatResult::BufferTooSmall;
  std::memcpy(out, row, n);
  return FormatResult::Ok;
}

enum class WriteResult { Unavailable, Accepted, Failed };
class Sink {
 public:
  virtual ~Sink() = default;
  // Accepted means ownership accepted by adapter, not power-loss durability.
  virtual WriteResult write(const Record&) = 0;
};
enum class Health { Unknown, Unavailable, Failed, Healthy };
template<size_t Capacity> class Queue {
  static_assert(Capacity > 0, "Queue must have capacity");
 public:
  bool push(const Record& record) {
    if (count_ == Capacity) {
      size_t victim = 0;
      while (victim < count_ && entries_[victim].important()) ++victim;
      ++dropped_;
      if (victim == count_) return false; // All important: retain history, reject incoming.
      // Input may alias an entry exposed by front(); shifting would change it.
      // One fixed-size snapshot, needed only on the eviction path.
      const Record incoming = record;
      for (size_t i = victim + 1; i < count_; ++i) entries_[i - 1] = entries_[i];
      --count_;
      entries_[count_++] = incoming;
      return true;
    }
    entries_[count_++] = record;
    return true;
  }
  // Exactly one attempt per call. Caller controls cadence/backoff; no retries here.
  WriteResult service(Sink& sink) {
    if (!count_) return WriteResult::Unavailable; // No health change for no work.
    const WriteResult result = sink.write(entries_[0]);
    if (result == WriteResult::Accepted) {
      if (health_ == Health::Failed || health_ == Health::Unavailable) ++recoveries_;
      health_ = Health::Healthy;
      for (size_t i = 1; i < count_; ++i) entries_[i - 1] = entries_[i];
      --count_;
    } else {
      health_ = result == WriteResult::Failed ? Health::Failed : Health::Unavailable;
      if (result == WriteResult::Failed) ++writeFailures_;
    }
    return result;
  }
  size_t size() const { return count_; }
  const Record* front() const { return count_ ? &entries_[0] : nullptr; }
  uint64_t dropped() const { return dropped_; }
  uint64_t writeFailures() const { return writeFailures_; }
  uint64_t recoveries() const { return recoveries_; }
  Health health() const { return health_; }
 private:
  Record entries_[Capacity];
  size_t count_ = 0;
  uint64_t dropped_ = 0, writeFailures_ = 0, recoveries_ = 0;
  Health health_ = Health::Unknown;
};
} // namespace logging
