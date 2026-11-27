#include "logging_core.h"
#include <cassert>
#include <limits>
#include <string>

using namespace logging;
struct FakeSink : Sink {
  WriteResult result = WriteResult::Unavailable;
  unsigned calls = 0;
  Record seen;
  WriteResult write(const Record& r) override { ++calls; seen = r; return result; }
};
// Compare serialized content, not object padding or only the sequence number.
static bool sameRecord(const Record& a, const Record& b) {
  char left[512], right[512]; size_t needed;
  assert(formatCsv(a, left, sizeof left, needed) == FormatResult::Ok);
  assert(formatCsv(b, right, sizeof right, needed) == FormatResult::Ok);
  return std::string(left) == right;
}
int main() {
  Factory factory("journey-1"); Record t, e;
  assert(factory.ready());
  assert(factory.temperature(t, 0, 4.25f, Status::Normal, true));
  assert(factory.event(e, 1, Event::Boot, "start"));
  assert(t.sequence == 1 && e.sequence == 2);
  assert(std::string(t.bootId) == "journey-1");
  char out[512]; size_t required;
  assert(formatCsv(t, out, sizeof out, required) == FormatResult::Ok);
  assert(std::string(out) == "\"journey-1\",1,0,4.25,NORMAL,1\r\n");
  const std::string expected(out);
  assert(formatCsv(t, out, expected.size(), required) == FormatResult::BufferTooSmall);
  assert(out[0] == 0 && required == expected.size() + 1);
  assert(formatCsv(t, out, expected.size()+1, required) == FormatResult::Ok);
  assert(formatCsv(t, nullptr, 0, required) == FormatResult::BufferTooSmall);
  for (Status status : {Status::Low, Status::High}) {
    assert(factory.temperature(t, 2, -1.25f, status, true));
    assert(formatCsv(t, out, sizeof out, required) == FormatResult::Ok);
    assert(std::string(out).find(statusName(status)) != std::string::npos);
    assert(std::string(out).find("-1.25") != std::string::npos);
  }
  for (float value : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(), 0.0f}) {
    assert(factory.temperature(t, 3, value, Status::SensorError, false));
    assert(formatCsv(t, out, sizeof out, required) == FormatResult::Ok);
    assert(std::string(out).find(",3,,SENSOR_ERROR,0\r\n") != std::string::npos);
  }
  assert(factory.temperature(t, 4, std::numeric_limits<float>::quiet_NaN(), Status::Normal, true));
  assert(!t.valid && t.status == Status::SensorError);
  Factory escaped("b,\"\n");
  assert(escaped.event(e, UINT32_MAX, Event::SmsSubmitted, "a,\"b\"\r\nc"));
  assert(formatCsv(e, out, sizeof out, required) == FormatResult::Ok);
  assert(std::string(out) == "\"b,\"\"\n\",1,4294967295,SMS_SUBMITTED,\"a,\"\"b\"\"\r\nc\"\r\n");
  const char* expectedNames[] = {"BOOT", "TEMPERATURE_HIGH", "TEMPERATURE_LOW",
      "TEMPERATURE_RECOVERED", "SENSOR_ERROR", "SENSOR_RECOVERED", "ALERT_REMINDER",
      "SMS_POLICY_EVENT", "SMS_ATTEMPT", "SMS_SUBMITTED", "SMS_FAILED", "SMS_QUEUE_LOSS",
      "STORAGE_FAILURE", "STORAGE_RECOVERED", "LOG_RECORD_LOSS"};
  size_t eventIndex = 0;
  for (Event event : {Event::Boot, Event::TemperatureHigh, Event::TemperatureLow,
      Event::TemperatureRecovered, Event::SensorError, Event::SensorRecovered,
      Event::AlertReminder, Event::SmsPolicyEvent, Event::SmsAttempt,
      Event::SmsSubmitted, Event::SmsFailed, Event::SmsQueueLoss,
      Event::StorageFailure, Event::StorageRecovered, Event::LogRecordLoss}) {
    assert(factory.event(e, 0, event));
    assert(formatCsv(e, out, sizeof out, required) == FormatResult::Ok);
    assert(std::string(eventName(event)) == expectedNames[eventIndex]);
    assert(std::string(out).find(expectedNames[eventIndex++]) != std::string::npos);
  }
  Factory empty(""); Factory nullId(nullptr);
  Factory longId(std::string(BOOT_CAPACITY, 'x').c_str());
  assert(!empty.ready() && !nullId.ready() && !longId.ready());
  assert(!empty.event(e, 0, Event::Boot));
  assert(!factory.event(e, 0, Event::Boot, std::string(DETAILS_CAPACITY, 'x').c_str()));
  assert(factory.event(e, 0, Event::Boot, std::string(DETAILS_CAPACITY-1, '"').c_str()));
  assert(formatCsv(e, out, sizeof out, required) == FormatResult::Ok);
  Factory restart("journey-2");
  assert(restart.event(e, 0, Event::Boot) && e.sequence == 1);
  assert(factory.event(e, UINT32_MAX, Event::Boot)); auto before = e.sequence;
  assert(factory.event(e, 0, Event::Boot) && e.sequence == before + 1 && e.uptimeMs == 0);
  Schedule schedule;
  assert(schedule.due(0)); assert(!schedule.due(29999)); assert(schedule.due(30000));
  assert(schedule.due(100000)); assert(!schedule.due(100001));
  Schedule wrap(150);
  assert(wrap.due(UINT32_MAX-100)); assert(!wrap.due(48)); assert(wrap.due(49));
  Schedule custom(10); assert(custom.due(1)); assert(!custom.due(10)); assert(custom.due(11));
  Schedule every(0); assert(every.due(0) && every.due(0));
  Queue<3> queue;
  assert(factory.temperature(t, 0, 4, Status::Normal, true));
  assert(queue.push(e)); assert(queue.push(t)); assert(queue.push(e));
  assert(queue.push(e) && queue.size() == 3 && queue.dropped() == 1);
  assert(queue.front()->sequence == e.sequence); // Oldest important retained.
  assert(!queue.push(t) && !queue.push(e) && queue.dropped() == 3);
  FakeSink sink;
  assert(queue.service(sink) == WriteResult::Unavailable && queue.size() == 3);
  assert(queue.health() == Health::Unavailable && sink.calls == 1);
  sink.result = WriteResult::Failed;
  assert(queue.service(sink) == WriteResult::Failed && queue.size() == 3);
  assert(queue.writeFailures() == 1 && sink.calls == 2);
  sink.result = WriteResult::Accepted;
  assert(queue.service(sink) == WriteResult::Accepted && queue.size() == 2);
  assert(queue.recoveries() == 1 && queue.health() == Health::Healthy);
  queue.service(sink); queue.service(sink);
  assert(queue.size() == 0 && !queue.front()); auto calls = sink.calls;
  queue.service(sink); assert(sink.calls == calls && queue.recoveries() == 1);
  Queue<1> faults;
  assert(factory.temperature(t, 0, 0, Status::SensorError, false));
  assert(faults.push(t) && !faults.push(e) && faults.dropped() == 1);
  Record malformed = e; std::memset(malformed.bootId, 'x', sizeof malformed.bootId);
  assert(formatCsv(malformed, out, sizeof out, required) == FormatResult::InvalidRecord);
  assert(out[0] == 0);
  // Rejected construction consumes no ID, and the factory owns its boot text.
  char boot[] = "copied"; Factory owned(boot); boot[0] = 'X';
  assert(!owned.event(e, 0, Event::Boot, nullptr));
  assert(owned.event(e, 0, Event::Boot) && e.sequence == 1);
  assert(std::string(e.bootId) == "copied");
  // Eviction removes the oldest routine sample without reordering survivors.
  Queue<3> ordered;
  Record first, routine, last, incoming;
  assert(owned.event(first, 0, Event::TemperatureHigh));
  assert(owned.temperature(routine, 1, 4, Status::Normal, true));
  assert(owned.event(last, 2, Event::SensorRecovered));
  assert(owned.event(incoming, 3, Event::SmsFailed));
  assert(ordered.push(first) && ordered.push(routine) && ordered.push(last));
  assert(ordered.push(incoming) && ordered.dropped() == 1);
  sink.result = WriteResult::Failed;
  assert(ordered.service(sink) == WriteResult::Failed);
  assert(ordered.front()->sequence == first.sequence);
  sink.result = WriteResult::Accepted; ordered.service(sink);
  assert(ordered.front()->sequence == last.sequence); ordered.service(sink);
  assert(ordered.front()->sequence == incoming.sequence);
  // Ordinary 30-second scheduling across UINT32_MAX, including exact boundary.
  Schedule thirty;
  assert(thirty.due(UINT32_MAX - 10000U));
  assert(!thirty.due(19998U));
  assert(thirty.due(19999U));
  assert(!thirty.due(49998U));
  assert(thirty.due(49999U));

  // Snapshot every aliased position, including entries shifted over by eviction.
  Record a, b, c;
  assert(owned.temperature(a, 10, 4.25f, Status::Normal, true));
  assert(owned.event(b, 11, Event::TemperatureHigh, "B"));
  assert(owned.event(c, 12, Event::SensorError, "C"));
  const Record originals[] = {a, b, c};
  for (size_t position = 0; position < 3; ++position) {
    Queue<3> aliases;
    assert(aliases.push(a) && aliases.push(b) && aliases.push(c));
    // front() points into the queue's contiguous Record array.
    const Record& aliased = aliases.front()[position];
    assert(aliases.push(aliased));
    assert(aliases.size() == 3 && aliases.dropped() == 1);
    for (const Record& expectedRecord : {b, c, originals[position]}) {
      assert(sameRecord(*aliases.front(), expectedRecord));
      sink.result = WriteResult::Accepted;
      assert(aliases.service(sink) == WriteResult::Accepted);
      assert(sameRecord(sink.seen, expectedRecord));
    }
    assert(!aliases.front() && aliases.dropped() == 1);
  }
  // Smallest queue: eviction aliases its own destination; important rejection.
  Queue<1> single;
  assert(single.push(a) && single.push(*single.front()));
  assert(single.dropped() == 1 && sameRecord(*single.front(), a));
  assert(single.push(b) && single.dropped() == 2);
  assert(!single.push(*single.front()) && single.dropped() == 3);
  assert(sameRecord(*single.front(), b));

  // Failures preserve output and consume no sequence.
  Factory failures("failures"); Record preserved = b;
  assert(!failures.event(preserved, 0, static_cast<Event>(999)));
  assert(sameRecord(preserved, b));
  assert(!failures.temperature(preserved, 0, 4, static_cast<Status>(999), true));
  assert(sameRecord(preserved, b));
  assert(!failures.event(preserved, 0, Event::Boot, std::string(DETAILS_CAPACITY, 'x').c_str()));
  assert(sameRecord(preserved, b));
  assert(!empty.event(preserved, 0, Event::Boot));
  assert(sameRecord(preserved, b));
  assert(failures.event(preserved, 0, Event::Boot) && preserved.sequence == 1);

  // Guard bytes and every capacity up to exact fit, including zero and one.
  char guarded[514];
  assert(formatCsv(a, out, sizeof out, required) == FormatResult::Ok);
  const size_t exact = required;
  for (size_t capacity = 0; capacity <= exact; ++capacity) {
    std::memset(guarded, 'G', sizeof guarded);
    const auto result = formatCsv(a, guarded + 1, capacity, required);
    assert(required == exact && guarded[0] == 'G' && guarded[capacity + 1] == 'G');
    assert(result == (capacity < exact ? FormatResult::BufferTooSmall : FormatResult::Ok));
    if (capacity && capacity < exact) assert(guarded[1] == 0);
    if (!capacity) assert(guarded[1] == 'G');
  }
  auto invalid = [&](const Record& bad) {
    out[0] = 'X'; required = 99;
    assert(formatCsv(bad, out, sizeof out, required) == FormatResult::InvalidRecord);
    assert(out[0] == 0 && required == 0);
  };
  malformed = b; std::memset(malformed.details, 'x', sizeof malformed.details); invalid(malformed);
  malformed = a; malformed.kind = static_cast<Kind>(999); invalid(malformed);
  malformed = a; malformed.status = static_cast<Status>(999); invalid(malformed);
  malformed = b; malformed.event = static_cast<Event>(999); invalid(malformed);
  malformed = a; malformed.valid = false; invalid(malformed);
  malformed = a; malformed.status = Status::SensorError; invalid(malformed);
  malformed = a; malformed.temperatureC = std::numeric_limits<float>::infinity(); invalid(malformed);
  malformed = a; malformed.temperatureC = 1000001; invalid(malformed);
  Factory maximum(std::string(BOOT_CAPACITY-1, '"').c_str());
  assert(maximum.event(e, UINT32_MAX, Event::TemperatureRecovered,
      std::string(DETAILS_CAPACITY-1, '"').c_str()));
  assert(formatCsv(e, out, sizeof out, required) == FormatResult::Ok);

  Queue<2> recovery;
  assert(recovery.push(b) && recovery.push(c));
  for (unsigned cycle = 1; cycle <= 2; ++cycle) {
    const Record expectedFront = *recovery.front();
    const size_t count = recovery.size();
    sink.result = WriteResult::Failed;
    assert(recovery.service(sink) == WriteResult::Failed);
    assert(recovery.size() == count && sameRecord(*recovery.front(), expectedFront));
    assert(sameRecord(sink.seen, expectedFront) && recovery.writeFailures() == cycle);
    sink.result = WriteResult::Unavailable;
    assert(recovery.service(sink) == WriteResult::Unavailable);
    assert(recovery.size() == count && sameRecord(*recovery.front(), expectedFront));
    sink.result = WriteResult::Accepted;
    assert(recovery.service(sink) == WriteResult::Accepted);
    assert(sameRecord(sink.seen, expectedFront) && recovery.size() == count-1);
    assert(recovery.recoveries() == cycle && recovery.dropped() == 0);
  }
  return 0;
}
