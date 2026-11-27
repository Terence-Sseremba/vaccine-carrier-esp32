# VC-006B logging core

`include/logging_core.h` is header-only C++17 with no Arduino or hardware dependencies.
It is not connected to firmware. Monitoring remains at its existing 2-second cadence.

Create one `logging::Factory` per boot with a caller-owned boot identifier value.
The factory copies it into fixed storage (1–39 bytes plus NUL). Each successful
record creation assigns a shared 64-bit sequence starting at 1. Invalid arguments
return false, leave the output unchanged and consume no sequence. Sequence
exhaustion returns false rather than wrapping. Boot ID uniqueness is the caller's
responsibility. A reset creates a new sequence domain; no state persists here.

Pass completed samples with their existing classification to `temperature()`.
This core does not classify temperature or decide events. Nonfinite readings,
explicitly invalid samples, and SensorError status produce invalid records with
empty CSV temperature. Values such as the DS18B20 sentinel 85 C must already be
marked invalid by the caller's existing classifier. `event()` accepts all specified
logging categories and details up to 127 bytes; oversized text is rejected, never
truncated. SMS_SUBMITTED denotes modem acceptance, not recipient delivery.

`Schedule::due(now)` returns true on the first completed sample and subsequently
at the configured interval (default 30 seconds). Zero means every call. Delayed
calls produce one record, not catch-up bursts. Unsigned subtraction explicitly
handles ordinary uptime rollover provided actual elapsed time since the last
call that returned true (the scheduling anchor) remains below 2^32 milliseconds.
The interval between consecutive calls alone does not guarantee this bound.
After a full elapsed wrap the 32-bit clock cannot distinguish elapsed durations;
call frequently enough to evaluate a due sample before that bound is reached.
Raw 32-bit uptime is preserved in CSV; sequence orders rows across rollover.
No calendar time or RTC is provided.

CSV headers are `TEMPERATURE_HEADER` and `EVENT_HEADER`. `formatCsv()` returns
Ok, BufferTooSmall, or InvalidRecord, and reports required bytes including NUL
for a valid record. Failure clears nonempty output buffers. Text fields are always
quoted; quotes are doubled, embedded CR/LF preserved, rows end CRLF. Temperature
uses two decimals and rounding half away from zero, with a literal decimal point.
Formatting rejects valid temperatures beyond +/-1,000,000 C. Output is bounded;
512 bytes suffices for every supported row. Do not mutate public Record arrays
without preserving NUL termination and field invariants. CSV quoting does not
sanitize spreadsheet formulas in caller text.

`Queue<N>` allocates N records inline and never waits for storage. All event
records and invalid sensor samples are important. When full, evict the oldest
routine sample and append the new record. If all queued records are important,
reject the incoming record, even if important. Survivors retain creation order.
Every rejection or eviction increments `dropped()`, so sequence gaps are expected.
An incoming reference may alias a queued record. The eviction path snapshots that
record before shifting entries, using one fixed-size temporary and no allocation.
Callers must inspect push results and counters and report loss locally; the core
does not recursively generate LOG_RECORD_LOSS events into an already full queue.
No queue can guarantee retention under unlimited load. Counters are uint64_t and
can wrap after 2^64 increments. Neither factory nor queue is thread safe.

Implement `Sink::write(const Record&)` for a future storage adapter. It returns
Unavailable, Failed, or Accepted. `Queue::service()` makes at most one attempt;
only Accepted removes the head. Failed/Unavailable retain it. Empty service makes
no sink call and returns Unavailable without changing health. Health transitions,
failure count and recovery count are queryable; first success after Failed or
Unavailable increments recoveries. Caller controls retries/backoff and observes
health changes to emit STORAGE_FAILURE/RECOVERED; no automatic retry loop exists.
Accepted means adapter acceptance, not durable persistence. Partial/ambiguous
writes require adapter policy using boot ID/sequence to recognize duplicates.
Sink calls themselves may block: this interface does not make SD nonblocking.

VC-006C still requires approval of actual module/voltages, SPI pins, queue size,
flush and retry cadence, boot ID generation, duplicate/partial-row recovery, and
storage-health presentation. RAM records are lost on restart. No physical storage
or hardware behavior is validated by these tests.
