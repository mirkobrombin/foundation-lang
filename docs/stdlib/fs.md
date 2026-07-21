# `std.fs`

`std.fs` is the bounded bootstrap filesystem surface. Paths are UTF-8 Foundation Strings; the
runtime converts them to the native representation and never exposes native path pointers.

```foundation
fn Size(path String) Result<u64, Error>
fn IsDirectory(path String) Result<bool, Error>
fn Modified(path String) Result<u64, Error>
fn OpenLines(path String) Result<own LineReader, Error>
fn OpenDir(path String) Result<own DirReader, Error>
fn LineReader.NextLimited(&self, limit u64) Result<Option<String>, Error>
task ReadText($path String) Result<String, Error>
task ReadTextLimited($path String, limit u64) Result<String, Error>
task ReadPrivateTextLimited($path String, limit u64) Result<String, Error>
task CreatePrivateDirectory($path String) Result<void, Error>
task WritePrivateTextAtomic($path String, $value String, limit u64) Result<void, Error>
```

`LineReader.Next` returns one owned line at a time as
`Result<Option<String>, Error>`. It strips LF and one preceding CR, validates UTF-8, distinguishes
EOF from an empty line, and caps each line at 16 MiB. `NextLimited` accepts a caller-selected byte
limit and consumes an oversized line before returning `LineTooLong`. Neither operation loads the
file into memory.

`ReadText` is spawned like any other task and keeps regular-file access off the cooperative
executor. It validates UTF-8 and caps input at 16 MiB. `ReadTextLimited` uses a caller-provided
byte cap and returns `TooLarge` before the file can grow the result beyond that cap. Regular files
use the bounded blocking executor because portable readiness APIs do not make their reads
non-blocking. Socket and watcher packages use callback reactor adapters instead.

`DirReader.Next` returns one owned entry name at a time, omits `.` and `..`, and does not promise
native enumeration order. Callers that need deterministic output sort collected paths before
display or serialization. `IsDirectory` follows the platform stat operation.
`Modified` returns whole Unix seconds from the same native metadata source.

`ReadPrivateTextLimited` accepts only regular files and refuses symbolic links on POSIX and
reparse points on Windows. `CreatePrivateDirectory` creates missing ancestors without changing
permissions on pre-existing ancestors. POSIX applies owner-only permissions to each new directory
and to the requested final directory; empty paths and the POSIX filesystem root are rejected.
Windows uses the account ACL inherited by newly created directories because POSIX mode bits do not
exist there.

`WritePrivateTextAtomic` rejects content above the supplied byte limit, writes a temporary file in
the destination directory, flushes it, and replaces the destination with one rename operation.
POSIX files are mode `0600`; Windows files inherit the private directory ACL. A failed write removes
the temporary file and leaves the previous destination intact.

Both readers own opaque runtime handles. `Close` is idempotent at the Foundation layer, and custom
drop closes any live handle during normal scope cleanup. The bootstrap handle representation is an
internal `u64`; it is private to `std.fs` and is not a public FFI contract.

The error enum distinguishes `NotFound`, `Permission`, `InvalidPath`, `InvalidUtf8`,
`LineTooLong`, `TooLarge`, `Closed`, and `Io`. Allocation failure remains a fatal panic.
