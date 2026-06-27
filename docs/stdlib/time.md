# `std.time`

`std.time` separates wall-clock instants from monotonic elapsed-time measurement.

```foundation
fn Now() Instant
fn FromUnix(seconds u64) Instant
fn Instant.FormatUtc(self) Result<String, Error>
fn MonotonicNow() MonotonicInstant
fn MonotonicInstant.Since(self, earlier MonotonicInstant) Result<Duration, Error>
fn Nanoseconds(value u64) Duration
fn Milliseconds(value u64) Duration
fn Seconds(value u64) Duration
```

`Instant.Unix` exposes the stored value. `Before` and `After` compare instants. `Since` returns an
explicit error when the receiver precedes the reference instant. `FormatUtc` produces a fixed
`YYYY-MM-DDTHH:MM:SSZ` representation and rejects timestamps outside the platform calendar range.
Local timezone policy remains outside this package.

`MonotonicNow` cannot be converted to calendar time. Its `Since` result has nanosecond precision,
while wall-clock `Instant.Since` preserves Unix-second precision. Keeping the clocks separate means
wall-clock corrections cannot corrupt lifecycle and health duration measurements.

The duration constructors support explicit deadlines without exposing representation fields.
`Milliseconds` saturates at the largest duration representable as nanoseconds instead of wrapping.
