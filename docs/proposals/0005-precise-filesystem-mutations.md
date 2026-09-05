# 0005: Precise filesystem mutations

Status: accepted

## User problem

Remote filesystems and file protocols must distinguish an occupied path from permission, type, and
storage failures. They also need exclusive creation, resizing, and rename operations that stay
confined beneath an already opened root.

## Design

`CreateRootFile` creates one new regular file and returns `AlreadyExists` when the path is occupied.
`ResizeFile` changes the length of an owned writable file without changing its current offset.
`RootWriter.CreateDirectoryEntry` creates exactly one directory with requested portable
permissions. `RootWriter.Rename` moves one non-link entry within the same root without replacing an
existing destination. Detailed file and empty-directory removal methods return `HostError` instead
of collapsing host categories into `fs.Error`.

```foundation
const opening = spawn fs.CreateRootFile($root, "partial.bin")
const opened = $opening.wait()
const fs.RootFileOpenOutcome { Root Value } = opened
const file = Value else return
```

Every relative path uses the existing root confinement rules. Rename rejects symbolic source
entries, cross-device moves, and platforms or filesystems without an atomic no-replace operation.

## Compatibility

All Foundation APIs and runtime entry points are additive. `FileMode` keeps its existing variants,
so exhaustive Language 1 matches continue to compile. The original removal methods keep their
error mapping. Existing native library and plugin ABIs remain valid.

## Diagnostics

No compiler diagnostics change. Runtime failures use the existing `HostError` variants.

## Implementation

POSIX uses descriptor-relative operations, `ftruncate`, and an atomic no-replace rename when the
host provides one. Windows uses held directory guards, exclusive creation, `SetEndOfFile`, and
`MoveFileExW` without replacement. The runtime restores the Windows file offset after resizing.

## Tests

Runtime tests cover occupied paths, exclusive creation, resize growth and shrink, no-replace
rename, cleanup, and allocation balance. Foundation fixtures exercise the owned file outcomes and
root confinement on supported targets.

## Alternatives

Checking whether a destination exists before rename has a race. Reusing `Truncate` would destroy an
occupied file. Expanding `FileMode` with a new variant would break exhaustive matches in existing
Language 1 source, so exclusive creation is a separate additive operation.
