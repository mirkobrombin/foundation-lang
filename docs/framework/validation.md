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

    @validation.Pattern("^[A-Z][a-z]+$")
    Code String

    @validation.Nested()
    Address Address

    @validation.Min(-10.5)
    @validation.Max(10.5)
    Score f64
}

methods Registration {
    @validation.Rule()
    fn businessRules(self, &errors validation.Errors) void {
        if self.Name == self.Email {
            errors.Add(.Custom, "Email", "must differ from name")
        }
    }
}
```

The compiler synthesizes `Validate(self) own validation.Errors` directly into the package semantic
model. No generated Foundation file or generator command is part of the project. This works in
libraries without `main`, services, routes, or an application host, including the first clean
checkout where callers of `Validate` are already present. The language server consumes the same
derived symbols as `check`, `test`, and `build`.

Field rules run in source field and attribute order, followed by custom rule methods in declaration
order. A successful validation keeps the error list empty and does not allocate an error node. Each
failure records a typed `ErrorKind`, the field path, and the stable message. `Errors.Add`, `Len`,
`IsEmpty`, and `TakeFirst` preserve source order and explicit ownership when callers inspect or
forward the collection.

`Required` accepts `String` and `List<T>` fields because those types have an unambiguous empty state.
It is rejected on numeric and boolean fields. This deliberately avoids pretending that zero means
"missing", the ambiguity that makes the v2 numeric `required` rule a no-op. `Min` and `Max` accept
every integer and floating-point field. Integer limits must be integral and fit the field type;
floating-point limits retain their source literal. A minimum above the maximum is a compile-time
error. `Email` accepts `String` and follows the ASCII shape used by the v2 built-in rule.

`Pattern` accepts `String` fields and uses the same portable bounded syntax as `std.pattern`:
start and end anchors, `.`, escaped literals, character classes and ranges, negated classes, and
the `?`, `*`, and `+` quantifiers. Unsupported grouping, alternation, and counted repetition are
compile-time errors. Matching has a fixed upper work budget, so an application cannot introduce an
unbounded regular-expression search through validation metadata. Patterns are limited to 1,024
bytes.

`Nested` requires another concrete `@Validatable` struct. Its errors are merged at the field's
position and receive paths such as `Address.City`. Compile-time cycle detection prevents recursive
validation. `Rule` requires exactly `fn name(self, &errors validation.Errors) void`; generic,
asynchronous, consuming, or otherwise mismatched methods are rejected. The method can inspect
multiple fields and report typed `.Custom` errors without a mutable global registry.

A typed `@web.Body()` model marked `@validation.Validatable()` is validated after strict JSON
binding and before its route function runs. Direct dispatch preserves
`FoundationWebError.Validation(errors)` with the complete typed collection. The HTTP transport
consumes that error and returns status 422 with a generic body. Binding syntax and type failures
remain 400, while an unsupported media type remains 415.

The shared compatibility fixture executes the common rule set against Foundation Lang and
Foundation v2 revision `06679f06495151fbd0d491e76121ba98b939a291`. The migration guide is
`docs/migrations/validation-v2.md`. The Lang surface intentionally replaces reflection tags and the
mutable custom-rule registry with typed compile-time declarations.
