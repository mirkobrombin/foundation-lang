# `std.net`

`std.net` provides portable TCP clients. Host names and transmitted text are UTF-8 Foundation
Strings. Native addresses and socket handles remain private to the runtime.

```foundation
task Connect(host String, port u64) Result<own TcpConnection, Error>
fn TcpConnection.Split(own) Result<StreamPair, Error>
task ReadLine(reader own TcpReader) ReadLineOutcome
task ReadLineLimited(reader own TcpReader, limit u64) ReadLineOutcome
task WriteAll(writer own TcpWriter, text String) WriteOutcome
```

`Connect` resolves the host on the bounded blocking executor, then attempts the returned addresses
through the callback reactor. A successful call owns one connection. `Split` consumes that
connection and returns independently owned read and write halves, so one read task and one write
task can run at the same time without sharing mutable Foundation state.

`ReadLine` strips LF and one preceding CR, validates UTF-8, and caps the returned line at 16 MiB.
`ReadLineLimited` uses the caller's byte limit. The limit covers returned UTF-8 bytes, excluding
the removed line ending. `Option.None` means EOF before another line; `Some("")` is an empty line.
Invalid UTF-8 and oversized lines close the read half before returning an error.

`WriteAll` completes only after the whole String was transmitted. Cancellation or an I/O failure
may happen after a prefix reached the peer. Any non-success result closes the returned write half,
so callers cannot accidentally reuse a stream with an unknown protocol position.

`ReadLineOutcome` and `WriteOutcome` return the owned half beside the operation result. Dropping a
task still closes every owned handle through normal deterministic cleanup. The runtime service is
created on first use and stops after its final address, connection, and request are released.

The error enum distinguishes `InvalidAddress`, `ResolveFailed`, `Refused`, `Closed`, `Cancelled`,
`InvalidUtf8`, `LineTooLong`, and `Io`. Ports must be in the range 1 through 65535. Allocation
failure remains a fatal panic.

TLS is not part of this package yet. A future TLS package will wrap the same owned stream model
without exposing platform TLS handles to Foundation code.
