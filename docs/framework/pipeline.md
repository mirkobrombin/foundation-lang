# Runtime pipelines

Use a language `pipeline` declaration for a fixed transform chain. Use `foundation.pipeline` when
middleware must run before and after a terminal handler, stop the chain, or retain owned state.

The package separates construction from execution:

```foundation
import foundation.pipeline as pipes

var builder = pipes.New<Request, Response, RequestError>()
builder.Use($logging)
builder.UseStateful(own MetricsMiddleware { calls = 0 })
var application = builder.Then($dispatch)

const response = application.Process($request) else error {
    return .Err(error)
}
```

`Use` accepts this function shape:

```foundation
fn logging(
    $input Request,
    next fn($Request) Result<Response, RequestError>
) Result<Response, RequestError> {
    const response = next($input) else error {
        return .Err(error)
    }
    .Ok(response)
}
```

The first registered middleware is the outermost wrapper. A middleware may call `next` once,
return without calling it, or replace its result. Its input is owned, so forwarding and dropping
remain visible. Stateful middleware implements `Middleware<T, U, E>` and is restored after every
call. `Then` consumes the builder; attempting to register more middleware afterwards is a
use-after-move diagnostic.

The terminal handler is mandatory by construction. This intentionally replaces Foundation v2's
empty-pipeline zero result, which cannot be expressed safely for an arbitrary move-only output.
