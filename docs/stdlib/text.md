# `std.text`

`std.text` provides allocation-aware operations for immutable UTF-8 `String` values. Byte offsets
are explicit because protocol parsers and file formats address encoded bytes rather than Unicode
scalar positions.

```foundation
fn Copy(value String) String
fn ByteLen(value String) u64
fn IsEmpty(value String) bool
fn Contains(value String, part String) bool
fn StartsWith(value String, prefix String) bool
fn EndsWith(value String, suffix String) bool
fn Slice(value String, start u64, end u64) Result<String, RangeError>
fn ByteAt(value String, index u64) Result<u64, RangeError>
fn Find(value String, part String) Option<u64>
fn Compare(left String, right String) i32
fn Equal(left String, right String) bool
fn NewBuilder() own Builder
```

`Slice` uses a half-open byte range and requires both offsets to be UTF-8 code point boundaries.
It returns an owned copy. `ByteAt` intentionally returns an encoded byte from 0 through 255 and
does not claim to return a character. `Find` returns a byte offset.

`Compare` orders encoded bytes and normalizes its result to -1, 0, or 1. `Builder` appends valid
Strings and Unicode scalar values without repeated whole-String copies. `Finish` transfers one
owned String and closes the builder. Custom drop releases a builder that was not finished.
