# `foundation.caching`

`foundation.caching` provides typed local caches and byte-oriented distributed backends behind
owner tasks. Every operation returns a joined `Task<Result<...>>`; no cache value or mutation is
shared implicitly between executors.

## Typed in-memory cache

```foundation
import foundation.caching
import std.time

fn cloneProfile(value Profile) Profile {
    Profile { Name = text.Copy(value.Name) Age = value.Age }
}

const cache = caching.NewWith<Profile>(
    $cloneProfile,
    caching.Policy {
        DefaultTTL = time.Seconds(300)
        MaxEntries = 10000
    }
) else error {
    return .Err(error)
}
```

The clone function is required because Foundation values never acquire implicit deep-copy
semantics. `Get` uses it to return an independent value. `Set` transfers its value into the cache.
`Handle()` creates another owned sender for the same cache owner task and may move to a worker when
its generic value is transferable.

```foundation
const storing = cache.Set("profile:42", $profile, time.Zero())
$storing.wait() else error {
    return .Err(error)
}

const loading = cache.Get("profile:42")
const found = $loading.wait() else error {
    return .Err(error)
}
```

`time.Zero()` selects `Policy.DefaultTTL`. A positive write TTL overrides the default. Negative
configuration and write durations are rejected. `MaxEntries = 0` is unbounded; a positive limit
evicts the oldest write deterministically. Reads do not refresh write order. `Len` first removes
expired entries and reports live values only.

## Distributed byte backend

`DistributedCache<E>` is the backend contract. Values cross that boundary as independent owned
`bytes.Bytes` instances:

```foundation
contract DistributedCache<E> {
    fn Fork(self) own DistributedCache<E>
    fn Get(self, $key String) Task<Result<Option<own bytes.Bytes>, E>>
    fn Set(
        self,
        $key String,
        $value own bytes.Bytes,
        ttl time.Duration
    ) Task<Result<void, E>>
    fn Delete(self, $key String) Task<Result<void, E>>
}
```

`NewDistributed` supplies a local byte backend for tests, single-process applications, and backend
adapters. It copies stored bytes on every successful `Get`, so closing a returned value cannot
invalidate the stored entry.

## Typed bridge

`NewBridge<T, B, C>` combines any `DistributedCache<B>` with explicit encode and decode functions.
The bridge works with `foundation.serializer`, a protocol codec, or an application-specific wire
format. `BridgeError<B, C>` preserves transport, backend, encode, and decode failures as separate
typed variants.

```foundation
const bridge = caching.NewBridge<
    Profile,
    caching.Error,
    serializer.Error
>(
    backend.Fork(),
    $encodeProfile,
    $decodeProfile
)
```

The complete serializer-backed program is in `examples/caching`.
