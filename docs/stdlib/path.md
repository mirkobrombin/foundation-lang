# `std.path`

`std.path` manipulates platform paths without exposing C macros or native path encodings.

`std.path` is available on every target. Under `freestanding` it uses `/` separators
and POSIX roots.

```foundation
fn Separator() String
fn Join(left String, right String) String
```

`Separator` returns `/` on Linux and macOS and `\` on Windows. `Join` returns an owned String,
preserves an existing separator, and removes one duplicated separator at the boundary. Empty
inputs copy the other side. Filesystem access remains in `std.fs`; path operations perform no I/O.
