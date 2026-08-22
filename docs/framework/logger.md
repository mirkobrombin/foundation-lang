# Logger

`foundation.logger` keeps severity, fields, trace identity, delivery, and shutdown policy visible.
The small `Logger` API writes deterministic text to standard output:

```foundation
import foundation.logger

var log = logger.NewWith(.Info)
log.Bind(logger.Field { Key = "service" Value = "catalog" })
log.BindTrace(logger.Trace { TraceID = "4bf92f" SpanID = "00f067" })

const fields = logger.WithField(logger.Fields(), "request", "r-42")
log.Info("served", $fields)
```

`Log`, `Debug`, `Info`, `Warn`, and `Error` consume the per-call field list. A call below the
configured level produces no entry. A per-call field replaces a bound field with the same key.
`BindTrace` and `ClearTrace` replace ambient string-key contexts with an explicit typed value.

## Typed sinks

`Sink<E>` consumes an owned `Entry` and returns its transport error. `Pipeline<E>` joins one sink
to the logger policy without erasing `E` or ignoring failures. `ConsoleSink` writes compact JSON;
`CLEFSink` writes CLEF with `@t`, `@m`, the CLEF severity mapping, flattened fields, `TraceId`, and
`SpanId`. Information entries omit `@l`.

```foundation
var output = logger.NewPipeline<logger.RenderError>(
    logger.NewConsoleSink(),
    .Info
)
output.Bind(logger.Field { Key = "service" Value = "catalog" })

const emitted = output.Info("ready", logger.Fields()) else error {
    discard error
    return 1
}
if !emitted return 2
```

Custom transports implement `Sink<E>`. Foundation does not catch a sink panic. Recoverable
transport failures belong in `E` and remain visible to the caller.

## Rendering

`NewEntry` constructs an entry without threshold filtering. `Logger.Prepare` applies the current
threshold, bound fields, current time, and trace identity. `PrepareAt` accepts a timestamp for
replay and deterministic tests. `RenderText`, `RenderJSON`, and `RenderCLEF` consume the entry they
encode. CLEF rejects caller fields named `@t`, `@m`, `@l`, `TraceId`, or `SpanId` instead of
silently replacing them.

## Asynchronous delivery

`NewAsync<E>` owns one sink worker and a bounded channel. `Publish` is a task: it waits when the
queue is full, so the caller may await it, race it against a timeout, or cancel its scope. Entries
are never dropped to hide overload. `Shutdown` closes the internal producer, drains accepted
entries, and returns sink errors in delivery order. Drop every cloned `Publisher` before calling
`Shutdown`; a live producer keeps the queue open. `Cancel` stops the worker without a drain
guarantee.

This deliberately replaces v2's hidden drop-on-full queue and ignored sink errors with owned
lifecycle and explicit backpressure.
