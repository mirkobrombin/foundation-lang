# `std.time`

`std.time` separates wall-clock instants from monotonic elapsed-time measurement.

```foundation
fn Now() Instant
fn FromUnix(seconds u64) Instant
fn Instant.FormatUtc(self) Result<String, Error>
fn MonotonicNow() MonotonicInstant
fn MonotonicInstant.Since(self, earlier MonotonicInstant) Result<Duration, Error>
fn Duration.Parse(value String) Result<Duration, DurationError>
fn Duration.Seconds(self) i64
fn Duration.Nanoseconds(self) i64
fn Duration.IsNegative(self) bool
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

`Duration` stores signed nanoseconds. `Parse` accepts signed decimal components using `ns`, `us`,
either Unicode microsecond spelling, `ms`, `s`, `m`, and `h`. Components may be combined, and a
fraction may appear in any component. Empty input, invalid syntax, and values outside signed 64-bit
nanoseconds are distinct errors.

The positive duration constructors support explicit deadlines without exposing representation
fields. Constructors saturate at the largest signed duration instead of wrapping. A negative
deadline passed to a Foundation operation is treated as an immediate deadline.
