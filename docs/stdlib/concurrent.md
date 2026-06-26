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

`sender.send(value)` and `receiver.receive()` are executable suspension points inside tasks. They
return typed `ChannelError` failures for closure and cancellation. A failed send consumes and
drops its payload; a successful send transfers ownership exactly once. `select` waits on multiple
send and receive operations without blocking the executor, chooses ready branches in source order,
and uses an explicit error branch plus a monotonic timeout branch.

## Blocking native work

The runtime owns a fixed worker pool for native operations that may block an operating-system
thread. Submitting work parks only the calling Foundation task. Completed jobs enter a thread-safe
queue and wake the originating executor; channel deadlines remain active while it waits.

Structured cancellation does not free a frame still used by foreign code. A non-cancellable call
finishes first, then the resumed task observes its cancellation flag. Adapters for cancellable APIs
pass an explicit native token instead of assuming that a thread can be stopped safely. The
source declaration is explicit:

```foundation
@blocking
extern c fn nativeRead(handle u64) String as package_native_read

task read(handle u64) String {
    const value = nativeRead(handle)
    value
}
```

`@blocking` is compiler-owned and has no parentheses. It applies only to a bodyless C ABI import.
The call must be a standalone binding or `discard` inside a task. ABI-safe arguments and the result
remain in the task frame while one generated callback runs on the bounded worker pool.

## Native callback reactor

Native APIs that already complete through callbacks do not consume a blocking worker. Their C ABI
adapter receives an opaque `fdn_reactor_operation` token from the generated Foundation bridge. The
source declaration names the native start symbol and may name its cancellation symbol:

```foundation
@callback(cancel = package_read_cancel)
extern c fn nativeRead(handle u64, result edit String) i32 as package_read_start

task read(handle u64) Result<String, ReadError> {
    var result = ""
    const status = nativeRead(handle, edit result)
    // The package converts status and result to its public typed error contract.
}
```

The generated C start signature returns `void` and appends the operation token:

```c
void package_read_start(uint64_t handle, fdn_string *result,
                        fdn_reactor_operation *operation);
void package_read_cancel(fdn_reactor_operation *operation);
```

The adapter stores the token with its native request and completes it later:

```c
void package_read_finished(struct package_read_request *request, int32_t status) {
    request->result = package_take_result(request);
    fdn_reactor_complete(request->operation, status);
}
```

The result must be fully published before completion, and every started operation must complete
exactly once. This remains true after a cancellation request. The optional cancellation callback
asks the native library to stop; it cannot release the operation token or task frame. Status values
belong to the adapter and are converted to a typed Foundation result by the generated bridge.

The token is opaque and valid only until its single completion is consumed. An adapter may store
and compare its pointer value only to identify the native request that owns it. It must not
dereference, order, perform pointer arithmetic on, free, or complete the token twice. Completion
may run on a foreign thread. The Foundation executor drains the completion queue and resumes the
owning task on its own thread.

`std.net` uses this contract for TCP connect, line reads, and complete writes. DNS resolution runs
on the blocking executor, while socket progress and cancellation remain reactor operations.

`@callback` applies only to a bodyless C ABI import with an `i32` Foundation result. A call is a
suspension point and must be a standalone binding or `discard` inside a task. It cannot be combined
with `@blocking` or used as a function value. The public package should keep raw status codes and
reactor details private and expose typed `Result` values instead.
