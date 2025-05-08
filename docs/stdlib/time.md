# `std.time`

`std.time` represents wall-clock instants and non-negative durations in whole Unix seconds.

```foundation
fn Now() Instant
fn FromUnix(seconds u64) Instant
fn Instant.FormatUtc(view) Result<String, Error>
```

`Instant.Unix` exposes the stored value. `Before` and `After` compare instants. `Since` returns an
explicit error when the receiver precedes the reference instant. `FormatUtc` produces a fixed
`YYYY-MM-DDTHH:MM:SSZ` representation and rejects timestamps outside the platform calendar range.
Local timezone policy remains outside this package.
