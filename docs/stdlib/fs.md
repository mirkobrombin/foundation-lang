# `std.fs`

`std.fs` is a read-only bootstrap filesystem surface. Paths are UTF-8 Foundation Strings; the
runtime converts them to the native representation and never exposes native path pointers.

```foundation
fn Size(path view String) Result<u64, Error>
fn IsDirectory(path view String) Result<bool, Error>
fn Modified(path view String) Result<u64, Error>
fn OpenLines(path view String) Result<own LineReader, Error>
fn OpenDir(path view String) Result<own DirReader, Error>
fn LineReader.NextLimited(edit, limit u64) Result<Option<String>, Error>
```

`LineReader.Next` returns one owned line at a time as
`Result<Option<String>, Error>`. It strips LF and one preceding CR, validates UTF-8, distinguishes
EOF from an empty line, and caps each line at 16 MiB. `NextLimited` accepts a caller-selected byte
limit and consumes an oversized line before returning `LineTooLong`. Neither operation loads the
file into memory.

`DirReader.Next` returns one owned entry name at a time, omits `.` and `..`, and does not promise
native enumeration order. Callers that need deterministic output sort collected paths before
display or serialization. `IsDirectory` follows the platform stat operation.
`Modified` returns whole Unix seconds from the same native metadata source.

Both readers own opaque runtime handles. `Close` is idempotent at the Foundation layer, and custom
drop closes any live handle during normal scope cleanup. The bootstrap handle representation is an
internal `u64`; it is private to `std.fs` and is not a public FFI contract.

The error enum distinguishes `NotFound`, `Permission`, `InvalidPath`, `InvalidUtf8`,
`LineTooLong`, `Closed`, and `Io`. Allocation failure remains a fatal panic.
