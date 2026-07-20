# `foundation.resiliency`

`foundation.resiliency.RateLimiter` is a token bucket driven by the monotonic clock.
Construction validates a positive refill rate and burst capacity. A new limiter begins with the
complete burst capacity, `Allow` consumes one token, and elapsed time replenishes tokens up to the
configured burst. `Wait` is a cooperative task. It returns the limiter with its typed result so a
single mutable bucket never becomes shared state by accident.

```foundation
import foundation.resiliency

const created = resiliency.NewRateLimiter(100, 20) else error {
    return .Err(error)
}
var limiter = created

if !limiter.Allow() {
    return .Err(.Busy)
}

const pending = spawn resiliency.Wait($limiter)
const resiliency.RateLimitWait { Limiter Value } = $pending.wait()
```

`CircuitBreaker<E>` keeps operation failures typed. It moves through `Closed`, `Open`, and
`HalfOpen`, admits one probe after the monotonic open duration, and invokes an optional typed state
callback after each transition. An open circuit is distinct from `Operation(error E)`.

```foundation
const created = resiliency.NewCircuitBreaker<ApiError>(3, time.Seconds(30)) else error {
    return .Err(error)
}
var circuit = created

circuit.Execute(callRemote) else error {
    return .Err(error)
}
```

`Retry<T, E>` accepts a typed operation and `RetryOptions<E>`. Attempts include the first call.
Delay grows by the finite factor, is capped before optional jitter, and waits cooperatively. A
`RetryIf` function may stop on a typed error. Dropping the task cancels a pending delay.

```foundation
const options = resiliency.RetryOptions<ApiError> {
    Attempts = 5
    InitialDelay = time.Milliseconds(50)
    MaxDelay = time.Seconds(2)
    Factor = 2.0
    Jitter = 0.2
    RetryIf = .Some(isTransient)
}
const pending = spawn resiliency.Retry<Response, ApiError>($request, $options)
const response = $pending.wait() else error {
    return .Err(error)
}
```

`Bulkhead` admits at most `maxConcurrent` permits and queues at most `maxQueue` tasks in FIFO order.
`Acquire` returns a task, queue overflow is `QueueFull`, and dropping a queued task removes it from
the queue. An owned `Permit` releases its slot on `Release` or lexical drop, including every early
return path.

```foundation
const created = resiliency.NewBulkhead(8, 32) else error {
    return .Err(error)
}
var gate = created
const pending = gate.Acquire()
const acquired = $pending.wait() else error {
    return .Err(error)
}
var permit = acquired
const response = callRemote()
permit.Release()
```

`foundation.resiliency.web.RateLimit<E>` creates stateful middleware owned by a manual
`foundation.web.Router<E>`. Each numeric `Request.RemoteAddress` receives an independent bucket.
The middleware returns HTTP 429 with `rate limit exceeded` without calling the next handler when a
bucket is empty. Empty addresses share the explicit `unknown` bucket. Idle clients expire after
ten minutes and the store retains at most 10,000 clients, evicting the least recently used entry.

```foundation
import foundation.resiliency.web as rateWeb

const middleware = rateWeb.RateLimit<ApiError>(100, 20) else error {
    return .Err(error)
}
router.UseStateful(10, $middleware) else error {
    return .Err(error)
}
```

`RateLimitWithPolicy<E>` accepts an explicit positive client TTL and maximum client count when the
defaults are unsuitable. The same owned middleware may be registered globally, for a static group,
or for one method and route through the three stateful router registration methods.

The router also preserves stateless function middleware through `Use`. Stateful middleware
implements `web.Middleware<E>` and receives an editable receiver, so mutable policy remains owned
by the application graph instead of escaping through a borrowed closure.

Foundation uses task cancellation and owned permits instead
of ambient contexts and callback-scoped slot release.
