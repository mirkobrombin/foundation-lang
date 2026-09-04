# 0001: Controlled TCP listeners

Status: accepted

## User problem

A server transfers its `TcpListener` into `Accept`. The lifecycle owner then has no capability to
close the listening socket, so joining an idle accept task can wait forever.

## Design

`TcpListener.Control` returns an owned `TcpListenerController`. Its `Close` method closes the shared
listening socket and completes every pending accept with `Error.Closed`. Dropping a controller
without calling `Close` does not affect the listener.

```foundation
const listener = net.Listen("127.0.0.1", 0) else return
const controllerValue = listener.Control() else return
var controller = controllerValue
const accepting = spawn net.Accept($listener)
controller.Close()
const outcome = $accepting.wait()
```

The listener and each controller hold one native reference. Closing either capability closes the
socket once. Native storage remains alive until all references are released.

## Compatibility

Existing Language 1 source and the current listener ABI keep their behavior. The runtime exports
three additive functions for acquiring, closing, and releasing a controller. Native libraries and
plugins do not need recompilation.

## Diagnostics

No diagnostics change.

## Implementation

The C runtime protects listener state with a platform mutex, reference-counts owners, marks pending
accept requests closed, and wakes the network reactor. `std.net` exposes the owned controller and
its deterministic drop path.

## Tests

The runtime test covers controller acquisition, release without closure, closure, and final native
cleanup. The accepted Foundation fixture closes a controller while `Accept` is pending and checks
for `Error.Closed` on both native backends.

## Alternatives

Task cancellation alone does not express server shutdown and can leave the listening socket owned
by the suspended task. Sharing the listener itself would weaken the ownership boundary. A separate
capability preserves one listener owner while making shutdown explicit.
