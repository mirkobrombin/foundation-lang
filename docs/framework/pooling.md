# `foundation.pooling`

`foundation.pooling` reuses transferable owned values without global state or GC-defined
retention. One task owns the retained list; every public operation joins through a typed result.

```foundation
@concurrent.Transferable()
struct Scratch {
    Used i32
}

fn fresh() Scratch {
    Scratch { Used = 0 }
}

fn clear(&value Scratch) void {
    value.Used = 0
}

const pool = pooling.NewWith<Scratch>(
    $fresh,
    pooling.Policy<Scratch> {
        MaximumRetained = 32
        Finalize = .Some($clear)
    }
) else error {
    return .Err(error)
}
```

`Get` returns a retained value or invokes the factory. `Put` runs the finalizer first, then retains
the value when capacity remains. An overflow value is released immediately after the finalizer.

```foundation
const loading = pool.Get()
var scratch = $loading.wait() else error {
    return .Err(error)
}

scratch.Used = 4096

const returning = pool.Put($scratch)
$returning.wait() else error {
    return .Err(error)
}
```

`New` uses a deterministic limit of 64 retained items. `NewWith` accepts any non-negative limit;
zero disables retention. `Handle()` creates another sender to the same owner task. Values and
captured factory or finalizer environments must satisfy `transferable` before they can cross the
task boundary.
