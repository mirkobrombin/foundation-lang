# Typed errors

`std.errors` aggregates recoverable errors without erasing their payload type. It does not catch
panics and does not attach hidden stack state to `Result` values.

```foundation
var failures = errors.NewMany<ValidationError>()
failures.Append(.MissingName)
failures.Append(.InvalidEmail)

const checked = $failures.Finish()
match checked {
    Ok: continueWork()
    Err(all): report($all.IntoList())
}
```

`Many<E>` preserves append order. `Message(render)` formats zero, one, or several values with an
explicit typed renderer. `IntoList` consumes the aggregate. `Finish` returns `Ok` for an empty
aggregate and `Err(Many<E>)` otherwise.

`Join($values)` applies the same rule to an ordered `List<E>`. `WithCode` creates an inspectable
`Coded<E>` value, and `CodedMessage` renders the familiar `[code] message` form.

An error value is never nil, so `Append` has no filtering rule. A panic remains terminal and the
runtime prints its complete Foundation source trace. HTTP code must map recoverable `Result`
failures explicitly instead of recovering a panic.
