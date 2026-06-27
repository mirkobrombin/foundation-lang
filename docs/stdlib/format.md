# `std.format`

`std.format` converts primitive values to owned Strings.

```foundation
fn Bool(value bool) String
fn I8(value i8) String
fn I16(value i16) String
fn I32(value i32) String
fn I64(value i64) String
fn Isize(value isize) String
fn U8(value u8) String
fn U16(value u16) String
fn U32(value u32) String
fn U64(value u64) String
fn Usize(value usize) String
fn F32(value f32) String
fn F64(value f64) String
```

Integer output is decimal, locale-independent, and covers the complete primitive range. Floating
output uses 9 significant digits for `f32` and 17 for `f64`, enough to recover the original binary
value. It uses `.` regardless of the process locale and spells special values `NaN`, `Infinity`, and
`-Infinity`.
