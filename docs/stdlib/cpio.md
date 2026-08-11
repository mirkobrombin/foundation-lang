# `std.cpio`

`std.cpio` reads, writes, packs, and extracts the portable CPIO `newc` format. Archives and entry
payloads use owned `std.bytes.Bytes`; no API treats binary data as UTF-8 text.

```foundation
import std.bytes
import std.cpio

const opened = cpio.NewWriter() else error {
    return .Err(error)
}
var writer = opened
writer.AddDir("etc", 493) else error {
    return .Err(error)
}
writer.AddFile("etc/app.conf", 420, bytes.FromText("enabled=true\n")) else error {
    return .Err(error)
}
const archive = $writer.Finish() else error {
    return .Err(error)
}
```

`Reader.Next` validates the 110-byte header, hexadecimal fields, name terminator, UTF-8 name,
four-byte alignment, payload bounds, configured totals, and the mandatory `TRAILER!!!` entry.
After the trailer it returns `End` repeatedly. A physical end before the trailer is
`MissingTrailer`; a partial header, name, payload, or padding is `UnexpectedEnd`.

Default reader limits are a 4 KiB encoded name, 64 MiB per file, 512 MiB total data, and 100,000
entries. Writer defaults add a 1 GiB bound for the complete encoded archive. Explicit zero limits
are rejected before allocation. A writer also rejects a name bound smaller than the 11-byte
trailer, an entry count that cannot fit the `newc` inode sequence, and an archive budget smaller
than the 124-byte trailer. Every entry is checked before the writer mutates its builder.

Writer names are canonical relative archive paths. They use forward slashes, contain no empty,
`.` or `..` component, and cannot contain a backslash or zero byte. The writer does not silently
trim or clean caller input. Permission arguments contain only the lower nine portable mode bits.
UID, GID, and modification time are fixed by `WriterOptions`, so identical inputs produce identical
archive bytes.

`PackDir` enumerates paths in stable bytewise order. Directories use `0755`; regular files use
`0755` when their executable bit is set and `0644` otherwise. Symbolic links, reparse points, and
special files return `UnsupportedType` and are never followed.

`UnpackToDir` accepts only regular files and directories. It validates every archive name again,
holds the destination root, refuses symbolic-link components, and writes regular files through an
atomic temporary file. A malformed archive or failed filesystem operation returns a typed error;
partial success is never reported as success.
