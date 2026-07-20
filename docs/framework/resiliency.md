# `foundation.resiliency`

`foundation.resiliency.RateLimiter` is a nonblocking token bucket driven by the monotonic clock.
Construction validates a positive refill rate and burst capacity. A new limiter begins with the
complete burst capacity, `Allow` consumes one token, and elapsed time replenishes tokens up to the
configured burst.

```foundation
import foundation.resiliency

var limiter = resiliency.NewRateLimiter(100, 20) else error {
    return .Err(error)
}

if !limiter.Allow() {
    return .Err(.Busy)
}
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

`Bulkhead` remains pending in this package.
