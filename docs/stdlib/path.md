# `std.path`

`std.path` manipulates platform paths without exposing C macros or native path encodings.

```foundation
fn Separator() String
fn Join(left view String, right view String) String
```

`Separator` returns `/` on Linux and macOS and `\` on Windows. `Join` returns an owned String,
preserves an existing separator, and removes one duplicated separator at the boundary. Empty
inputs copy the other side. Filesystem access remains in `std.fs`; path operations perform no I/O.
