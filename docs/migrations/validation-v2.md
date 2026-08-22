# Migrating Foundation v2 validation

Foundation Lang replaces `core/validation` struct tags, reflection, and the mutable rule registry
with typed attributes and compiler-derived methods.

## Models

Foundation v2:

```go
type Registration struct {
    Name  string  `validate:"required"`
    Age   int     `validate:"min=18,max=99"`
    Email string  `validate:"email"`
    Code  string  `validate:"pattern=^[A-Z][a-z]+$"`
    Score float64 `validate:"min=-10.5,max=10.5"`
}

errors := validation.New().Validate(registration)
```

Foundation Lang:

```foundation
@validation.Validatable()
struct Registration {
    @validation.Required()
    Name String

    @validation.Min(18)
    @validation.Max(99)
    Age i32

    @validation.Email()
    Email String

    @validation.Pattern("^[A-Z][a-z]+$")
    Code String

    @validation.Min(-10.5)
    @validation.Max(10.5)
    Score f64
}

var errors = registration.Validate()
```

The compiler checks attribute targets, parameter types, numeric fit, range order, pattern syntax,
method collisions, nested cycles, and custom rule signatures. There is no validator instance,
reflection cache, string rule lookup, or generator command.

## Rule mapping

| Foundation v2 | Foundation Lang | Migration |
| --- | --- | --- |
| `required` on String, slice, or map | `@validation.Required()` on `String` or `List<T>` | Direct replacement |
| `required` on a number | No numeric Required rule | Model absence with `Option<T>` or validate the domain value explicitly |
| `min=n` | `@validation.Min(n)` | Direct replacement with compile-time numeric fit checks |
| `max=n` | `@validation.Max(n)` | Direct replacement with compile-time numeric fit checks |
| `email` | `@validation.Email()` | Direct replacement |
| `pattern=...` | `@validation.Pattern("...")` | Use the portable bounded pattern subset |
| `Register(name, rule)` | `@validation.Rule()` method | Replace process-wide string registration with a typed method |
| Nested validation in caller code | `@validation.Nested()` | Add the attribute to the nested field |

The portable pattern syntax supports anchors, one-byte wildcard, escaped literals, character
classes, ranges, negated classes, and the `?`, `*`, and `+` quantifiers. It rejects grouping,
alternation, lookaround, backreferences, and counted repetition. Expressions are limited to 1,024
bytes and matching has a fixed work budget.

## Custom rules

Foundation v2 registers a callback under a string and places that string in a struct tag.
Foundation Lang keeps the rule beside the model:

```foundation
methods Registration {
    @validation.Rule()
    fn businessRules(self, &errors validation.Errors) void {
        if self.Name == self.Email {
            errors.Add(.Custom, "Email", "must differ from name")
        }
    }
}
```

The signature is exact. The rule receives a read-only model and the editable error collection. It
runs after field rules and can inspect multiple fields. Exported registries, duplicate rule names,
late replacement, and stale reflection caches have no Foundation Lang equivalent.

## Nested models

Mark both models as validatable and place `@validation.Nested()` on the containing field. Child
errors keep their typed kind and receive a path such as `Address.City`. Recursive validation cycles
are compile-time errors.

## Consuming errors

`Validate` returns an owned `validation.Errors`. `Len` and `IsEmpty` inspect it, while `TakeFirst`
consumes errors in source order. Each `validation.Error` contains a typed `Kind`, `Field`, and
`Message`. Code that switched on v2 message strings should switch on `ErrorKind` instead.

The web body pipeline performs this validation after strict binding and before route dispatch.
Validation failures retain the complete typed collection and map to HTTP 422.

## Compatibility proof

`tests/compatibility/validation/cases.tsv` is the common input for a Foundation Lang runner and a
Go runner linked to Foundation v2 revision `06679f06495151fbd0d491e76121ba98b939a291`. The CTest
fixture normalizes both products to typed field results, compares each with the canonical output,
and rejects a changed or dirty v2 baseline.

```sh
cmake --preset dev \
  -DFOUNDATION_V2_SOURCE=/path/to/go-foundation
cmake --build --preset dev
ctest --test-dir build/dev --output-on-failure \
  -R '^compatibility\.validation\.v2$'
```
