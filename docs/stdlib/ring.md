# `std.ring`

`std.ring.Buffer<T>` is a bounded, non-thread-safe FIFO. Construction allocates the complete slot
pool. `Push`, `Pop`, `Peek`, `Reset`, `Cap`, `Len`, and `Space` perform no further allocation.

```foundation
import std.ring

var jobs = ring.New<String>(32) else error {
    return .Err(error)
}

jobs.Push("compile") else full {
    return queueElsewhere(full)
}

discard jobs.Peek(showNext)
const next = jobs.Pop()
```

The public surface is:

```foundation
enum ConfigurationError {
    ZeroCapacity
}

enum PushError<T> {
    Full(value T)
}

fn New<T>(capacity i32) Result<own Buffer<T>, ConfigurationError>
fn Cap(self) i32
fn Len(self) i32
fn Space(self) i32
fn IsEmpty(self) bool
fn Push(&self, $value T) Result<void, PushError<T>>
fn Pop(&self) Option<T>
fn Peek(&self, operation fn(T) void) bool
fn Drain(&self) own collections.Queue<T>
fn Reset(&self) void
```

A full `Push` returns the unaccepted value inside `PushError.Full`; it never destroys a caller's
move-only value. `Peek` loans the oldest element to a callback for the duration of the call. The
callback cannot retain that loan, and the ring remains unchanged. `Pop` transfers the oldest value
to the caller. `Drain` transfers all values in FIFO order into an owned queue and clears the ring.
Creating a ring with a zero or negative capacity returns `ConfigurationError.ZeroCapacity`.

`ByteBuffer` specializes the same slot pool for `u8` and preserves partial read and write behavior:

```foundation
fn NewBytes(capacity i32) Result<own ByteBuffer, ConfigurationError>
fn Cap(self) i32
fn Len(self) i32
fn Space(self) i32
fn Write(&self, source [u8]) usize
fn Read(&self, &destination [u8]) usize
fn Reset(&self) void
```

`Write` stops when the source ends or the buffer fills. `Read` stops when the destination fills or
the buffer empties. Both return the number of transferred bytes and allocate nothing after
construction.

The package does not add locking. Share a ring through one owned worker or protect it with an
explicit synchronization boundary.
