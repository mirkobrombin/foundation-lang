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

The compiler adds typed methods directly to the package-owned struct. There is no generation step
or project file to maintain:

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

## Named sources and defaults

`@bind.From(source, key)` maps a field to a named source. It is repeatable; attribute order is the
static fallback order. `@bind.Default(value)` applies only when every source is absent or empty.

```foundation
@bind.Bindable()
struct RequestOptions {
    @bind.From("path", "id")
    Id u64

    @bind.From("query", "page")
    @bind.Default("1")
    Page u32
}
```

The generator adds a second method when a field has source metadata:

```foundation
fn BindSources(&self, &sources bind.Sources) Result<void, bind.Error>
```

`Sources.Set` registers evaluated values. The first non-empty value registered for the same source
and key wins. This keeps source acquisition outside the binder, so request adapters and custom
callbacks do not become stored closures with hidden lifetimes.

```foundation
var sources = bind.NewSources()
sources.Set("path", "id", "42")
sources.Set("query", "page", "")

const result = options.BindSources(&sources)
```

## Strict JSON

Every bindable struct receives a generated `BindJSON` method:

```foundation
fn BindJSON(&self, source String) Result<void, bind.Error>
```

The method accepts one JSON object, preserves fields absent from the object, and decodes each
present property according to its declared Foundation type. `@bind.JsonName(...)` changes only the
JSON property name; it does not change keys used by `Bind` or `BindSources`. String lists require a
JSON array of strings and replace their previous contents.

Parsing is strict. Duplicate properties, trailing data or a second JSON value, non-object roots,
wrong property types, and unknown properties return typed `bind.Error` values. Syntax errors retain
the underlying `json.ErrorKind` and parser offset. Unknown-field errors retain the first
unconsumed property name.

To preserve the request body-field boundary, mark one local bindable field with `@bind.JSON()`:

```foundation
@bind.Bindable()
struct CreateProfile {
    @bind.JsonName("display_name")
    DisplayName String

    Age u8
}

@bind.Bindable()
struct RequestInput {
    RequestId String

    @bind.JSON()
    Body CreateProfile
}
```

`RequestInput.BindJSON` delegates to `Body.BindJSON`; other request fields remain unchanged. The
compiler rejects multiple JSON body fields, non-bindable body types, empty JSON names, duplicate
JSON names, and hand-written method collisions before generating source.
