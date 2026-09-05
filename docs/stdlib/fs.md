# `std.fs`

`std.fs` is the bounded filesystem surface. Paths are UTF-8 Foundation Strings; the
runtime converts them to the native representation and never exposes native path pointers.

```foundation
fn Size(path String) Result<u64, Error>
fn IsDirectory(path String) Result<bool, Error>
fn Modified(path String) Result<u64, Error>
fn OpenLines(path String) Result<own LineReader, Error>
fn OpenDir(path String) Result<own DirReader, Error>
fn OpenTree(path String, maxEntries u64, maxPathLength u64) Result<own TreeReader, Error>
fn OpenRoot(path String) Result<own RootWriter, Error>
task OpenFile($path String, mode FileMode) Result<own File, HostError>
task OpenRootFile($root own RootWriter, $path String, mode FileMode) RootFileOpenOutcome
task CreateRootFile($root own RootWriter, $path String) RootFileOpenOutcome
task ReadFileChunk($file own File, limit u64) FileReadOutcome
task WriteFileChunk($file own File, $value own bytes.Bytes) FileOutcome
task SeekFile($file own File, offset u64) FileOutcome
task ResizeFile($file own File, size u64) FileOutcome
task SizeFile($file own File) FileSizeOutcome
task SyncFile($file own File) FileOutcome
fn LineReader.NextLimited(&self, limit u64) Result<Option<String>, Error>
fn TreeReader.Next(&self) Result<Option<TreeEntry>, Error>
fn TreeReader.ReadFile(self, path String, limit u64) Result<own bytes.Bytes, Error>
fn TreeReader.ReadSymbolicLink(self, path String, limit u64) Result<SymbolicLinkInfo, Error>
fn RootWriter.CreateDirectory(&self, path String) Result<void, Error>
fn RootWriter.CreateDirectoryEntry(&self, path String, permissions u32) Result<void, HostError>
fn RootWriter.WriteFile(self, path String, value bytes.Bytes, permissions u32) Result<void, Error>
fn RootWriter.CreateSymbolicLink(&self, path String, target String, directory bool) Result<void, Error>
fn RootWriter.RemoveFile(&self, path String) Result<void, Error>
fn RootWriter.RemoveEmptyDirectory(&self, path String) Result<void, Error>
fn RootWriter.RemoveFileDetailed(&self, path String) Result<void, HostError>
fn RootWriter.RemoveEmptyDirectoryDetailed(&self, path String) Result<void, HostError>
fn RootWriter.Rename(&self, source String, destination String) Result<void, HostError>
fn RootWriter.SetPermissions(&self, path String, permissions u32) Result<void, Error>
fn RootWriter.SetModified(&self, path String, modified u64) Result<void, Error>
task ReadText($path String) Result<String, Error>
task ReadTextLimited($path String, limit u64) Result<String, Error>
task ReadPrivateTextLimited($path String, limit u64) Result<String, Error>
task CreatePrivateDirectory($path String) Result<void, Error>
task WritePrivateTextAtomic($path String, $value String, limit u64) Result<void, Error>
task DeletePrivateFile($path String) Result<void, Error>
task WatchNext($path String, intervalMilliseconds u64) Result<Event, Error>
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

`WatchNext` waits for one observed change at the supplied path through the callback reactor.
It returns an `Event` with `Created`, `Modified`, or `Removed` and the observed path. The interval
must be greater than zero; zero returns `InvalidInterval`. A watch observes one next change, does
not promise recursive coverage or event coalescing, and must be called again for a later event.
The portable polling provider accepts at most 64 simultaneous watches and returns `ResourceLimit`
instead of creating an unbounded number of native threads.

`DirReader.Next` returns one owned entry name at a time, omits `.` and `..`, and does not promise
native enumeration order. Callers that need deterministic output sort collected paths before
display or serialization. `IsDirectory` follows the platform stat operation.
`Modified` returns whole Unix seconds from the same native metadata source.

`TreeReader` takes a stable bytewise snapshot of relative paths. It applies entry-count and path
length bounds before returning the handle. Each entry is classified as `File`, `Directory`,
`SymbolicLink`, or `Other`; traversal never follows symbolic links or Windows reparse points.
Entries carry their lower nine portable permission bits and whole-second Unix modification time.
`ReadFile` accepts only a regular file beneath the held root, revalidates every path component, and
enforces the caller's byte limit before adopting the result as owned `std.bytes.Bytes`.
`ReadSymbolicLink` applies the same path checks, returns the stored UTF-8 target without following
it, and enforces a caller-selected byte limit. Its directory flag preserves the native Windows
link kind; POSIX filesystems do not store that distinction and report `false`.

`RootWriter` confines every operation beneath one held destination root. Relative paths reject
empty components, `.`, `..`, backslashes, absolute roots, and embedded zero bytes. Directory
creation refuses symbolic links and reparse points. `CreateDirectory` creates missing descendants;
`CreateDirectoryEntry` creates one directory and returns `AlreadyExists` when its path is occupied.
File writes use an exclusive temporary file, flush its contents, and replace the destination
atomically. `Rename` moves one entry between relative paths under the same root and fails when the
destination exists. POSIX applies the requested lower nine permission bits; Windows keeps the
destination ACL because POSIX modes do not exist there. Symbolic-link creation requires an existing
safe parent. Removal distinguishes files and links from empty directories and never follows a link.
The `Detailed` removal variants retain native error categories while the original methods preserve
the Language 1 API. Metadata changes reject links. POSIX applies all lower nine permission bits;
Windows maps absence of write bits to the read-only attribute. Both platforms store modification
time in whole Unix seconds.

`File` streams regular binary files through the bounded blocking executor. `ReadFileChunk`
returns at most the requested positive byte count and distinguishes clean EOF with `None`.
`WriteFileChunk` writes the complete supplied value. `SeekFile` uses an absolute byte offset,
`ResizeFile` changes the file length without changing the current offset, `SizeFile` reads the
current length, and `SyncFile` flushes written data before an explicit close. `Write` mode creates a
missing file and preserves existing
contents; `Truncate` creates or empties it. `CreateRootFile` creates a new file beneath a held root
and fails with `AlreadyExists` when the path is occupied. Reads and writes reject symbolic final
components and non-regular files.

`OpenRootFile` applies the same streaming operations to a relative path beneath a held
`RootWriter`. It validates every relative component and holds parent directories against
replacement while opening the file. Call `RootWriter.CreateDirectory` first when a writable
nested parent does not exist. The returned file remains valid after the root closes.

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

`DeletePrivateFile` removes only a regular file. It refuses symbolic links and Windows reparse
points and returns `NotFound` when the path does not exist. Higher-level stores may deliberately map
that condition to idempotent deletion.

All readers and roots own opaque runtime handles. `Close` is idempotent at the Foundation layer,
and custom drop closes any live handle during normal scope cleanup. The native handle
representation is an internal `u64`; it is private to `std.fs` and is not a public FFI contract.

The error enum distinguishes `NotFound`, `Permission`, `InvalidPath`, `InvalidUtf8`,
`LineTooLong`, `TooLarge`, `InvalidInterval`, `ResourceLimit`, `Cancelled`, `Closed`, and `Io`. Allocation
failure remains a fatal panic.
