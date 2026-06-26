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

The current executor remains cooperative. A separate bounded parallel worker pool is required
before the v2 `core/worker` compatibility row can close.
