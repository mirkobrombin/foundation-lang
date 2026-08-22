# `foundation.lock`

`foundation.lock` coordinates keyed in-process leases through one owner task. Handles clone the
request endpoint and may cross worker executors. Storage, owner tokens, TTL expiry, waiter order,
and release decisions remain serialized by that task.

`Locker` and `Lease` are contracts. `InMemoryLocker` is the built-in local implementation returned
by `New`; remote providers can implement the contracts in adapter packages without changing the
language or this owner task.

```foundation
import foundation.lock
import std.time

const locker = lock.New()
const pending = locker.Acquire("profile:42", time.Seconds(5))
const acquired = $pending.wait()
const lease = acquired else error {
    discard error
    panic("lock acquisition failed")
}

const releasing = lease.Release()
discard $releasing.wait()
```

`Acquire` waits cooperatively and returns `Cancelled` when its task is cancelled. `TryLock` returns
`Option<own Lease>` immediately: `Some` acquired the key and `None` found another owner. A positive
TTL releases only the matching owner token. An expired or already released lease cannot unlock a
newer owner of the same key.

Waiters are granted in request order. If a waiting task is cancelled after enqueueing, its closed
reply endpoint is skipped when the key becomes available. Releasing the same lease more than once
is successful and has no additional effect.

A lease that is dropped without `Release` remains held until its positive TTL expires or the locker
is dropped. This matches the explicit release contract of Foundation v2 and keeps destruction free
of hidden task creation. Code without a bounded TTL must always wait for `Release`.
