# `std.concurrent`

`std.concurrent` defines the shared cancellation values used by tasks, IO, channels, and native
adapters. Cancellation is an observation, not an exception or an operation error.

```foundation
import std.concurrent

enum LoadError {
    Cancelled
}

task load(cancel concurrent.Cancellation) Result<String, LoadError> {
    if cancel.IsRequested() {
        return .Err(.Cancelled)
    }
    .Ok("value")
}

fn main() i32 {
    const source = concurrent.NewCancellationSource()
    const pending = spawn load(source.Token())
    source.Cancel()
    discard $pending.wait()
    0
}
```

`CancellationSource` owns one reference to the shared state. `Token()` returns a new owned
`Cancellation` reference. Moving a token into a task consumes that token, while the source remains
available to request cancellation. Dropping either value releases exactly one reference.

`Cancellation.IsRequested()` observes both its source and the current task's structured
cancellation state. Dropping a live task handle therefore reaches code that already checks its
token; APIs do not need a second cancellation mechanism for structured shutdown.

The shared request flag uses release/acquire synchronization. Its native representation is an
opaque `u64` handle private to the standard library and is not a public package ABI.

## Channels

`channel<T>(capacity)` returns a move-only `Channel<T>` pair. A complete
`const Channel { sender receiver }` pattern transfers each direction into its own lexical owner.
The compiler rejects borrowed payloads and emits cleanup for the pair and both endpoint types.

The runtime transport for `Sender<T>` and `Receiver<T>` is implemented. A zero-capacity channel
performs a rendezvous; a positive capacity stores values in FIFO order. Sender and receiver counts
are independent, buffered owned values use generated drop glue, and closing either direction wakes
operations that can no longer complete. Structured task cancellation removes a parked operation
from its channel queue before resuming it as cancelled.

Send, receive, and `select` lowering remain pending. Endpoint creation and deterministic close are
already executable language behavior.
