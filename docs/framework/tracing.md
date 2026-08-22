# Tracing

`foundation.tracing` keeps trace propagation visible in ordinary function signatures. A tracer
receives an owned parent context and returns the child context with the active span:

```fn
var tracer = tracing.Noop<AppError>()
var started = tracing.Start<AppError>(
    &tracer,
    tracing.Root(),
    "checkout",
    tracing.Attributes()
)

const failure = AppError.Unavailable
started.Span.RecordError($failure)
started.Span.End()
```

Real tracers implement `Tracer<E>` and produce an owned `Span<E>`. The error parameter remains the
application's type. Exporters do not receive an erased runtime error value.

## Typed attributes

`AttributeValue` accepts `Text`, `Bool`, `Signed`, `Unsigned`, and `Float`. Build an owned list with
`Attributes` and `WithAttribute`, then transfer it when starting or annotating a span:

```fn
const attributes = tracing.WithAttribute(
    tracing.Attributes(),
    tracing.Attribute {
        Key = "attempt"
        Value = .Signed(2)
    }
)
```

## Context ownership

`Context` contains trace and current span identity. Starting a span consumes the parent value and
returns its child value, which makes propagation and lifetime visible. `Root` creates an empty
context for a new trace.

`Noop<E>` is explicit. Foundation does not use a nullable tracer, hidden global provider, or
string-key context bag.
