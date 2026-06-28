# `std.parse`

`std.parse` converts validated text to primitive values without locale-dependent behavior.

```foundation
fn Bool(value String) Result<bool, BooleanError>
fn F32(value String) Result<f32, FloatError>
fn F64(value String) Result<f64, FloatError>
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

`Bool` trims Unicode White Space code points and compares ASCII letters without case. It accepts
`1`, `t`, `true`, `yes`, `y`, and `on` as true. It accepts `0`, `f`, `false`, `no`, `n`, and `off`
as false. Empty and invalid tokens are distinct errors.

`F32` and `F64` accept complete decimal and hexadecimal IEEE 754 input. Hexadecimal values require
a binary exponent. Underscores may separate digits. `NaN`, signed infinity, and their long
`Infinity` spelling are case-insensitive. Parsing always uses the C numeric locale, regardless of
the process locale. Empty, malformed, and out-of-range input produce distinct errors. Underflow
rounds to the target format and is not an error.
