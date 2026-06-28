# Typed struct binding

`foundation.bind` converts named string values into concrete struct fields through generated
Foundation source. It does not expose runtime reflection, `any`, or string tags.

## Declaring a binder

Apply `@bind.Bindable()` to a concrete struct. Every exported field uses its Foundation name by
default. Package-internal fields are never part of the binding surface. `@bind.Name(...)` selects
another key and `@bind.Ignore()` preserves an exported field without reading the source.

```foundation
import foundation.bind
import std.time

@bind.Bindable()
struct ServerConfig {
    Host String

    @bind.Name("port")
    Port u16

    Timeout time.Duration

    @bind.Ignore()
    Source String
}
```

Run the package generator after changing binding metadata:

```text
foundationc emit-app-host . -o src/zz_foundation.fdn
```

The generated file adds a typed method to the package-owned struct:

```foundation
fn Bind(&self, &source bind.Values) Result<void, bind.Error>
```

## Supplying values

`Values` owns its keys and values. The most recent `Set` for a key wins. A source remains
reusable after binding.

```foundation
var config = ServerConfig {
    Host = "127.0.0.1"
    Port = 8080
    Timeout = time.Zero()
    Source = "defaults"
}
var values = bind.NewValues()
values.Set("port", "9000")
values.Set("Timeout", "1.5s")

const result = config.Bind(&values)
```

Absent keys preserve the current field value. String list fields append one value. Supported
targets are `String`, `bool`, every fixed-width or pointer-width integer, `f32`, `f64`,
`std.time.Duration`, and `own std.collections.List<String>`.

`bind.Error` reports `Kind`, `Field`, `Key`, and the rejected `Value`. Unsupported fields,
duplicate keys, generic bindable structs, and collisions with a hand-written `Bind` method are
compile-time diagnostics. Use `@bind.Ignore()` when a field is intentionally outside the binding
surface.
