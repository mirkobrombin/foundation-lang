# `std.parse`

`std.parse` converts validated text to primitive values without locale-dependent behavior.

```foundation
fn U64(value view String) Result<u64, IntegerError>
```

The parser distinguishes empty input, non-decimal bytes, and overflow. It accepts the complete
`u64` range and does not ignore whitespace or signs.
