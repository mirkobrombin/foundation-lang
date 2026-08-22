# Migrate Foundation v2 CPIO

Read the source stream into owned bounded `std.bytes.Bytes`, then transfer it into
`cpio.NewReader`. Replace the `(*Entry, error)` loop and `io.EOF` check with exhaustive handling of
`Result<own Entry, cpio.Error>` and the `End` case.

Replace a writer's destination `io.Writer` with an in-memory `cpio.Writer`. Configure UID, GID,
modification time, and all resource limits in one `WriterOptions` value. Transfer the writer into
`Finish` and write or send the returned archive through the destination package.

Do not migrate non-canonical names unchanged. Remove leading roots and `./` components in caller
code, convert platform separators to `/`, and reject empty, `.` or `..` components before calling
the Foundation writer. Foundation does not normalize them silently.

Mode arguments use the lower nine permission bits as decimal `u32`: use `493` for `0755` and `420`
for `0644`. `PackDir` applies those values automatically and preserves only whether a regular file
is executable.

Treat `UnsupportedType` as a hard archive-policy failure. Foundation never follows or serializes a
symbolic link, Windows reparse point, or special file. `UnpackToDir` creates or holds its protected
root and refuses an existing symbolic-link component rather than writing through it.
