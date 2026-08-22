# `foundation.scheduler`

`foundation.scheduler` owns recurring and ad hoc tasks behind one actor. Cron expressions use the
five-field Foundation v2 subset: each minute, hour, day, month, and weekday field is either `*` or
one decimal value in range.

```foundation
fn NewJob(
    $name String,
    expression String,
    $handler transferable fn($concurrent.Cancellation) Task<void>
) Result<Job, Error>

fn New() own Scheduler
fn NewWithStore($store own JobStore) own Scheduler

fn Scheduler.Register(self, $job Job) Task<Result<void, Error>>
fn Scheduler.Start(self) Task<Result<void, Error>>
fn Scheduler.Enqueue(
    self,
    $factory transferable fn($concurrent.Cancellation) Task<void>
) Task<Result<void, Error>>
fn Scheduler.ScheduleAfter(
    self,
    delay time.Duration,
    $factory transferable fn($concurrent.Cancellation) Task<void>
) Task<Result<void, Error>>
fn Scheduler.Tick(self, instant time.Instant) Task<Result<void, Error>>
fn Scheduler.Shutdown($self) Result<void, Error>
fn Scheduler.Cancel($self) Result<void, Error>
```

A handler is a task factory rather than an already-spawned task. This guarantees that
`ScheduleAfter` creates work only after its delay expires. The cancellation token is shared with
the scheduler. `Shutdown` stops admission and drains accepted work. `Cancel` requests cancellation
and then joins it. Both consume the scheduler, so later use is a compile-time ownership error.

`Tick` exposes deterministic UTC evaluation for tests, simulations, and externally clocked hosts.
`Start` performs the same evaluation once per second with `std.time.Now`. A job runs at most once
per UTC minute. A restored record scans missed minutes for the next matching instant, capped at
five years like Foundation v2.

## Persistent state

```foundation
task NewJobStore($directory String) Result<own JobStore, StoreError>
fn JobStore.Save(self, $record JobRecord) Task<Result<void, StoreError>>
fn JobStore.Load(self, name String) Task<Result<JobRecord, StoreError>>
fn JobStore.List(self) Task<Result<own collections.List<JobRecord>, StoreError>>
```

The store rejects an empty directory and rejects empty record names, `.`, `..`, separators, and
embedded NUL bytes. Records are strict, bounded JSON. Saves use private atomic replacement. Loads
refuse symbolic links and Windows reparse points. Lists skip unrelated or invalid entries and
return records in stable name order.

`NewWithStore` loads `LastRun` during registration and persists successful task completion with UTC
time and monotonic latency. A persistence failure cancels the actor and is returned as
`Error.Store` when the owning scheduler is joined.
