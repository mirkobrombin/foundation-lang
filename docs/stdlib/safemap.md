# `std.safemap`

`std.safemap` owns its storage in one Foundation task. Cloned handles submit typed operations to
that owner, so `Set`, `GetOrSet`, `Compute`, snapshots, and expiry cleanup are serialized without
exposing mutable storage.

```foundation
import std.safemap

var counters = safemap.NewSharded<String, i32>(
    $safemap.StringEqual,
    $safemap.CloneString,
    $cloneI32,
    $safemap.StringHasher,
    16
)

const storing = counters.Set("builds", 40)
const stored = $storing.wait()
stored else error {
    discard error
    panic("cannot store counter")
}

const computing = counters.Compute("builds", 0, $increment)
const builds = $computing.wait() else error {
    discard error
    panic("cannot update counter")
}
```

The public constructors are:

```foundation
fn New<K, V>(
    $equal fn(K, K) bool,
    $cloneKey fn(K) K,
    $cloneValue fn(V) V
) own Map<K, V>

fn NewSharded<K, V>(
    $equal fn(K, K) bool,
    $cloneKey fn(K) K,
    $cloneValue fn(V) V,
    $hash fn(K) u64,
    shardCount i32
) own ShardedMap<K, V>
```

Foundation values may be move-only. The constructor therefore requires explicit key and value
clone functions instead of assuming that any generic value can be copied. `Get`, `Keys`, `Values`,
`Snapshot`, `GetOrSet`, and `Compute` use those functions when ownership leaves the map.

`Map<K, V>`, `ShardedMap<K, V>`, and `Handle<K, V>` expose the same operation set:

```foundation
fn Set(self, $key K, $value V) Task<Result<void, Error>>
fn Get(self, $key K) Task<Result<Option<V>, Error>>
fn Delete(self, $key K) Task<Result<bool, Error>>
fn Has(self, $key K) Task<Result<bool, Error>>
fn Len(self) Task<Result<i32, Error>>
fn Keys(self) Task<Result<own collections.List<K>, Error>>
fn Values(self) Task<Result<own collections.List<V>, Error>>
fn Snapshot(self) Task<Result<own collections.List<Pair<K, V>>, Error>>
fn Clear(self) Task<Result<void, Error>>
fn GetOrSet(self, $key K, $value V) Task<Result<V, Error>>
fn Compute(
    self,
    $key K,
    $initial V,
    $update transferable fn(V) V
) Task<Result<V, Error>>
```

Every operation is a task because it may wait for the owner. `GetOrSet` and `Compute` are atomic
with respect to every other handle. `Compute` takes an explicit initial value because Foundation
does not define an implicit zero for arbitrary generic types.

`ShardedMap.WithExpiry` changes the TTL applied to later writes. A zero duration disables expiry.
Expired entries are removed before each operation, and snapshots never include them. Shard counts
are rounded up to a power of two. `StringHasher` provides deterministic FNV-1a over UTF-8 bytes.

Handles are safe across cooperative tasks and native worker executors. Their request and reply
channels use the executor mailbox transport, so the actor remains the sole storage owner while
callers run concurrently on `foundation.worker.Pool`. The executable fixture covers concurrent
writers, reply channels, remote `select` wakeups, cleanup, and the function-valued `Compute`
operation.
