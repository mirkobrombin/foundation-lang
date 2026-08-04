# `foundation.adapters`

`foundation.adapters` provides a typed registry for named application adapters. One Foundation task
owns the mutable state. `Handle<T>` contains independently owned channel endpoints and may move to a
worker when `T` is transferable.

```foundation
import foundation.adapters
import std.text

fn cloneTransport(value Transport) Transport {
    Transport { Protocol = text.Copy(value.Protocol) }
}

const registry = adapters.New<Transport>($cloneTransport)
```

The clone function is mandatory. Foundation does not hide a shallow or deep copy behind `Get`,
`Default`, a callback, or an executor boundary. Every returned adapter and every callback argument
is produced through that function.

## Operations

`Register`, `Get`, `SetDefault`, `Has`, `Names`, `Remove`, `Clear`, `OnRegister`, and `OnRemove`
return joined tasks with explicit `Closed` or `Cancelled` failures. `MustGet` and `Default` retain
the convenience panic boundary and include Foundation source frames. `DefaultOr` returns its owned
fallback when no registered default exists.

```foundation
const registering = registry.Register("http", $transport)
const registered = $registering.wait()
match registered {
    Ok: {}
    Err(error): handleRegistryError(error)
}

const reading = registry.Get("http")
const found = $reading.wait()
```

`Names` is sorted by UTF-8 byte order. This makes generated output, tests, and inspection stable.
Removing a missing name succeeds and fires the current remove callbacks.
Removing the selected name also clears the default.

## Callbacks

Callbacks are transferable task factories. They run after the mutation is committed, in
registration order, outside the state owner. A callback can therefore call `Get`, `Has`, or another
registry operation without deadlocking.

```foundation
fn observe(
    $handle adapters.Handle<Transport>
) transferable fn($String, $Transport) Task<void> {
    fn($name String, $value Transport) Task<void> capture($handle) {
        spawn inspect(handle.Clone(), $name, $value)
    }
}
```

Each mutation captures the callbacks that belong to that event. `Clear` removes entries, the
default, and callbacks for future events. A callback sequence already in flight still completes
against its retained event value.

The complete executable is in `examples/adapters`.
