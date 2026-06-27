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

`Pool.Start` requires a direct `spawn` expression. Primitive values, `String`, arrays, and
aggregate values composed only from those types can transfer. Borrows, slices, function values,
contracts, task or channel handles, and structs with custom `drop` cannot cross into the pool.
This structural rule prevents a native resource handle or executor-local value from moving to a
different thread by accident.

Each worker owns a cooperative executor for the transferred task and any child tasks or channels
it creates. `Shutdown` drains submitted work and joins every worker. `Cancel` requests structured
cancellation for queued and running tasks, then joins. Cancellation remains cooperative while a
task is executing ordinary CPU instructions. A `print` call writes one complete line, while line
ordering between parallel tasks remains unspecified.
