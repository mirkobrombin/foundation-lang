# 0002: Concurrent web serving

Status: accepted

## User problem

`Server.ServeUntil` waits for a complete request before accepting the next connection. A client
that sends an incomplete request can therefore stop unrelated clients from reaching the server.

## Design

`Server.ServeUntil` accepts new connections while earlier connections are parsed and written by
owned tasks. Application dispatch remains serialized because `Application.Dispatch` holds one
exclusive edit loan over the application graph. `Server.ServeBatch` exposes the same behavior for
an exact number of accepted connections and returns the reusable server after every task finishes.

At most 256 connection tasks remain active. The accept loop waits for one task before admitting
more work at that limit. A failure on one established connection closes that connection without
stopping the listener. An accept failure still terminates continuous serving.

```foundation
const serving = server.ServeBatch(2)
```

## Compatibility

`ServeOne` keeps its existing ownership and error behavior. Existing `ServeUntil` calls compile
without changes. The new behavior prevents one established client from blocking later accepts.

## Diagnostics

No diagnostics change.

## Implementation

The web package owns connection tasks with `foundation.worker.Group`. A private application actor
preserves the exclusive dispatch loan and restores the application graph when a batch stops.
OpenSSL servers expose the same batch operation through their existing transport adapter.

## Tests

The web server fixture holds one request open, completes a second request, then finishes the first
and recovers the server. The fixture runs through native code generation and allocation checks.

## Alternatives

Cloning an application graph would require every service to define shared ownership. Making
`Application.Dispatch` a read loan would weaken the current state and middleware guarantees.
