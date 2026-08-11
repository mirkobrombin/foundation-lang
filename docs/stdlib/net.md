# `std.net`

`std.net` provides portable TCP clients and servers. Host names, local IP addresses, and
transmitted text are UTF-8 Foundation Strings. Native addresses and socket handles remain private
to the runtime.

```foundation
task Connect($host String, port u64) Result<own TcpConnection, Error>
fn DeadlineAfter(duration time.Duration) Result<Deadline, Error>
task ConnectUntil($host String, port u64, deadline Deadline) Result<own TcpConnection, Error>
fn Listen($address String, port u64) Result<own TcpListener, Error>
task Accept(listener own TcpListener) AcceptOutcome
fn TcpConnection.Split($self) Result<StreamPair, Error>
task ReadLine(reader own TcpReader) ReadLineOutcome
task ReadLineLimited(reader own TcpReader, limit u64) ReadLineOutcome
task ReadLineLimitedUntil(reader own TcpReader, limit u64, deadline Deadline) ReadLineOutcome
task ReadExact(reader own TcpReader, length u64) ReadOutcome
task ReadExactUntil(reader own TcpReader, length u64, deadline Deadline) ReadOutcome
task ReadExactBytes(reader own TcpReader, length u64) ReadBytesOutcome
task ReadExactBytesUntil(reader own TcpReader, length u64, deadline Deadline) ReadBytesOutcome
task WriteAll(writer own TcpWriter, $text String) WriteOutcome
task WriteAllUntil(writer own TcpWriter, $text String, deadline Deadline) WriteOutcome
task WriteAllBytes(writer own TcpWriter, value own bytes.Bytes) WriteOutcome
task WriteAllBytesUntil(writer own TcpWriter, value own bytes.Bytes, deadline Deadline) WriteOutcome
```

`Connect` resolves the host on the bounded blocking executor, then attempts the returned addresses
through the callback reactor. A successful call owns one connection. `Split` consumes that
connection and returns independently owned read and write halves, so one read task and one write
task can run at the same time without sharing mutable Foundation state.

`DeadlineAfter` converts one positive duration into an absolute monotonic `Deadline`. The four
`Until` operations share that value, so a protocol can enforce one deadline across connection,
write, and multiple reads instead of resetting a relative timeout at each suspension. Reactor poll
time is derived from the nearest active deadline. `Timeout` is distinct from task `Cancelled`.
Name resolution runs on the bounded blocking executor and cannot be interrupted inside a platform
resolver call, but an expired deadline prevents the following connection attempt.

`Listen` binds one IPv4 or IPv6 address. An empty address binds the IPv4 wildcard and port zero
asks the operating system to choose an available port. `TcpListener.Port` reports the actual bound
port. `Accept` waits through the callback reactor and returns the still-owned listener beside the
accepted connection, so repeated acceptance never relies on a hidden shared lifetime.

`ReadLine` strips LF and one preceding CR, validates UTF-8, and caps the returned line at 16 MiB.
`ReadLineLimited` uses the caller's byte limit. The limit covers returned UTF-8 bytes, excluding
the removed line ending. `Option.None` means EOF before another line; `Some("")` is an empty line.
Invalid UTF-8 and oversized lines close the read half before returning an error.

`ReadExact` waits for exactly the requested number of UTF-8 bytes. It consumes only those bytes
and preserves any following bytes for the next read. EOF before the requested length is an error.
The zero-length operation succeeds without waiting for the peer.

`ReadExactBytes` returns an owned `std.bytes.Bytes` value and never applies UTF-8 validation. It
preserves NUL, invalid UTF-8, and every other octet exactly. `ReadBytesOutcome` restores the reader
beside the owned payload or typed error. The deadline variant has the same exact-length and EOF
rules as the text operation.

`WriteAll` completes only after the whole String was transmitted. Cancellation or an I/O failure
may happen after a prefix reached the peer. Any non-success result closes the returned write half,
so callers cannot accidentally reuse a stream with an unknown protocol position.

`WriteAllBytes` transfers one owned byte value into the task, keeps its native storage alive until
the callback reactor finishes, and releases it before returning the writer. No text conversion or
copy is inserted by `std.net`. The caller can retain an independent replay value with
`bytes.Bytes.Copy` before starting the task.

`ReadLineOutcome`, `ReadOutcome`, `ReadBytesOutcome`, and `WriteOutcome` return the owned half beside
the operation result. Dropping a task still closes every owned handle through normal deterministic
cleanup. The runtime service is created on first use and stops after its final address, connection,
and request are released.

The error enum distinguishes `InvalidAddress`, `ResolveFailed`, `Refused`, `Closed`, `Cancelled`,
`InvalidUtf8`, `LineTooLong`, `AddressInUse`, `Timeout`, and `Io`. Connect ports must be in the
range 1 through 65535. Listen ports may also be zero. Allocation failure remains a fatal panic.

TLS is not part of this package yet. A future TLS package will wrap the same owned stream model
without exposing platform TLS handles to Foundation code.
