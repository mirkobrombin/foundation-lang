# `foundation.worker`

`foundation.worker` owns application work whose lifetime extends beyond the lexical scope that
created it. It is a framework package, not a second scheduler.

```foundation
import foundation.worker

task refresh() void {
    print("refresh")
}

fn main() i32 {
    const background = worker.NewSupervisor()
    background.Start(spawn refresh())
    background.Shutdown()
    0
}
```

`Start` transfers a `Task<void>` into the supervisor. `Shutdown` stops the lifetime and joins every
owned task without requesting cancellation. `Cancel` requests structured cancellation and then
joins. Dropping the supervisor follows the cancellation path.

Only `Task<void>` can be supervised. A task that performs a fallible operation must handle the
`Result`, send the error to an owned channel, or apply another explicit policy before it returns.
This keeps `Result` must-use intact after detachment.

`Group<T>` is the typed completion counterpart. It admits at most its declared capacity, owns
every task through a supervisor, and buffers exactly one completion per admitted task. `Next`
transfers the group into a task and returns both the first completed value and the still-owned
group:

```foundation
var group = worker.NewGroup<i32>(2)
discard group.Add(spawn compute(1))
discard group.Add(spawn compute(2))

const waiting = $group.Next()
const outcome = $waiting.wait()
const worker.GroupNext { Group Value } = outcome
group = Group
```

The ownership round trip prevents concurrent `Next` calls on one receiver. `Shutdown` joins and
discards unread completions, while `Cancel` requests cancellation before joining. `WaitOrStop`
selects between the first typed completion and a caller-owned `Receiver<void>` stop signal; it is
the coordination primitive used by `foundation.hosting.Run`.

The current executor remains cooperative. `Pool` provides bounded parallel execution for CPU work
that must use multiple cores:

```foundation
task render(name String) void {
    print(name)
}

fn main() i32 {
    const pool = worker.NewPool(4)
    pool.Start(spawn render("first"))
    pool.Start(spawn render("second"))
    pool.Shutdown()
    0
}
```

`Pool.Start` requires a direct `spawn` expression. Owned primitive values, `String`, arrays,
function values, directional channels, and aggregates composed from transferable fields can cross
into the pool. Borrows, slices, contracts, and task handles cannot cross. A custom-drop struct must
opt in with `@concurrent.Transferable()`, and every field must still pass the structural check.
This prevents a native resource handle or executor-local value from moving to another thread by
accident while preserving explicit transfer for safe containers.

Each worker owns a cooperative executor for the transferred task and any child tasks it creates.
Channels synchronize their state and route remote wakes through the destination executor mailbox,
so send, receive, and select can coordinate between a pool worker and another executor. `Shutdown`
drains submitted work and joins every worker. `Cancel` requests structured cancellation for queued
and running tasks, then joins. Cancellation remains cooperative while a task is executing ordinary
CPU instructions. A `print` call writes one complete line, while line ordering between parallel
tasks remains unspecified.
