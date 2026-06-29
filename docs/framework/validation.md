# `foundation.validation`

`foundation.validation` replaces runtime struct tags and reflection with typed field attributes and
a generated method on each concrete model.

```foundation
@validation.Validatable()
struct Registration {
    @validation.Required()
    Name String

    @validation.Min(18)
    @validation.Max(99)
    Age u8

    @validation.Required()
    @validation.Email()
    Email String
}
```

The compiler synthesizes `Validate(self) own validation.Errors` directly into the package semantic
model. No generated Foundation file or generator command is part of the project. This works in
libraries without `main`, services, routes, or an application host, including the first clean
checkout where callers of `Validate` are already present. The language server consumes the same
derived symbols as `check`, `test`, and `build`.

Rules run in source field and attribute order. A successful validation keeps the error list empty
and does not allocate an error node. Each failure records a typed `ErrorKind`, the field name, and
the stable message. `Errors.Len`, `IsEmpty`, and `TakeFirst` preserve explicit ownership when
callers inspect or forward the collection.

`Required` accepts `String` and `List<T>` fields because those types have an unambiguous empty state.
It is rejected on numeric and boolean fields. This deliberately avoids pretending that zero means
"missing", which is ambiguous. `Min` and `Max` accept
integer fields and inclusive `i64` limits; a negative limit is rejected for unsigned fields and a
minimum above the maximum is a compile-time error. `Email` accepts `String` and follows the ASCII
shape used by the v2 built-in rule.

A typed `@web.Body()` model marked `@validation.Validatable()` is validated after strict JSON
binding and before its route function runs. Direct dispatch preserves
`FoundationWebError.Validation(errors)` with the complete typed collection. The HTTP transport
consumes that error and returns status 422 with a generic body. Binding syntax and type failures
remain 400, while an unsupported media type remains 415.

The current package is not the completed v2 compatibility boundary. Floating-point limits,
portable pattern rules, custom rule contracts, nested model validation, and shared behavioral
fixtures against v2 remain pending.
