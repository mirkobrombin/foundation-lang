# `std.pattern`

`std.pattern` provides a portable bounded matcher implemented in Foundation source.

```foundation
import std.pattern

if pattern.Matches("Ada", "^[A-Z][a-z]+$") {
    print("valid")
}

if !pattern.IsValid("[unfinished") {
    print("invalid expression")
}
```

The supported syntax is deliberately smaller than a platform regex engine:

- `^` and `$` anchor the start and end.
- `.` matches one UTF-8 byte.
- `\\` escapes the next byte.
- `[abc]`, `[a-z]`, and `[^0-9]` define byte classes.
- `?`, `*`, and `+` quantify one preceding atom.

Grouping, alternation, lookaround, backreferences, and counted repetition are not supported. Invalid
syntax and expressions above 1,024 bytes return `false`. Each match has a work budget capped at
1,000,000 recursive steps. This keeps the result deterministic across C toolchains and prevents an
unbounded search on hostile input.

`foundation.validation.Pattern` statically validates constant patterns before generated validation
calls this matcher. `foundation.web` uses the same syntax for `regex(...)` route constraints, so a
route has identical behavior on every supported C toolchain.
