# 0006: Controlled OpenSSL listeners

Status: accepted

## User problem

An owning server cannot stop promptly while `OpenSSLAccept` is pending. Cancelling the task does not
interrupt the native `accept` call, so joining the server can wait forever when no client arrives.

## Design

`OpenSSLListener.Control` creates an independently owned stop capability before the listener moves
into an accept task. `OpenSSLListenerController.Close` closes the listening socket and wakes the
pending accept. The accept outcome still returns the listener, which can then be released normally.

```foundation
const controller = listener.Control() else error { return .Err(error) }
const accepting = spawn tls.OpenSSLAccept($listener)
discard controller.Close()
const accepted = $accepting.wait()
```

Dropping the controller without closing it leaves the listener active.

## Compatibility

Existing OpenSSL listener construction, acceptance and close calls do not change. The API is
additive and uses the provider's existing listener registry and close operation.

## Diagnostics

No diagnostics change.

## Implementation

`foundation.tls` exposes the controller. The OpenSSL provider's existing registry keeps listener
close safe while an accept operation holds a transient user reference. No runtime ABI changes.

## Tests

The OpenSSL provider fixture starts a pending accept, closes it through the controller and requires
the accept task to return an error instead of blocking.

## Alternatives

Polling accept with short timeouts adds idle wakeups and makes shutdown latency depend on a timer.
Task cancellation alone cannot interrupt the platform call.
