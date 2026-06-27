# `std.parse`

`std.parse` converts validated text to primitive values without locale-dependent behavior.

```foundation
fn I8(value String) Result<i8, IntegerError>
fn I16(value String) Result<i16, IntegerError>
fn I32(value String) Result<i32, IntegerError>
fn I64(value String) Result<i64, IntegerError>
fn Isize(value String) Result<isize, IntegerError>
fn U8(value String) Result<u8, IntegerError>
fn U16(value String) Result<u16, IntegerError>
fn U32(value String) Result<u32, IntegerError>
fn U64(value String) Result<u64, IntegerError>
fn Usize(value String) Result<usize, IntegerError>
```

The parsers distinguish empty input, non-decimal bytes, and overflow. They accept each complete
integer range and do not ignore whitespace. Signed parsers accept one leading `-`; unsigned parsers
reject signs. A leading `+` is not accepted.
