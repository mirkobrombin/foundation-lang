# `std.json`

`std.json` parses RFC 8259 values in Foundation source. It does not call a host JSON library.

```foundation
fn Parse(source String) Result<Value, Error>
```

`Value` has `Null`, `Bool`, `Number`, `Text`, `Array`, and `Object` variants. Numbers retain their
validated source spelling so parsing does not lose precision. `Array.PopFront` and `Object.Take`
transfer owned values without copying the remaining tree.

The parser validates escapes, UTF-16 surrogate pairs, number grammar, trailing input, duplicate
object keys, and a nesting limit of 128 containers. `Error` reports an `ErrorKind` and byte offset.
Input is borrowed for the parse; every String stored in the result is an owned copy.
