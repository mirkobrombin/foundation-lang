# 0010: Streaming child processes

Status: accepted

## User problem

A long-lived helper that speaks a framed or line-based protocol needs separate stdin, stdout, and
stderr pipes. `Command.Capture` waits for process exit and a PTY adds terminal echo, line
discipline, and merged output that corrupt machine protocols.

## Design

`std.process.Start` consumes a configured `Command` and returns a `Child` with five independent
owners: `Stdin`, `Stdout`, `Stderr`, `Controller`, and `Waiter`.

`Read` returns one bounded non-empty byte chunk, `None` at EOF, or a typed process error. `Write`
writes the complete owned byte value. Both operations restore their stream owner in an outcome.
`Writer.Close` closes stdin without terminating the child. `Wait` consumes the waiter and returns
the exit code. `Controller.Abort` terminates the child.

The runtime caps one read at 16 MiB. It does not decode text, frame messages, combine output, or
invoke a shell.

## Compatibility

The API adds Language 1 declarations and runtime symbols. Existing source and native artifacts do
not change. The runtime retains these symbols for future Language 1 toolchains.

## Diagnostics

No diagnostic codes change. Invalid read limits return `process.Error.InvalidArgument`. Operations
on a closed endpoint return `process.Error.Closed`.

## Implementation

The C11 runtime creates three anonymous pipes and confines inherited endpoints to the child. POSIX
uses `posix_spawnp` and a child process group. Windows uses an explicit inherited-handle list.
Foundation wrappers retain one native child until the final owner is dropped.

## Tests

Runtime tests write stdin, read stdout and stderr separately, observe EOF, wait for the exit code,
and prove that all native handles are released. Language fixtures run the same exchange through
the LLVM and C backends, then abort a second child and observe EOF from its output pipe. CI builds
the runtime on Linux, macOS, and Windows.

## Alternatives

Using a PTY changes byte transport semantics and merges stderr into terminal output. Extending
`Capture` with callbacks would put scheduling and protocol policy inside the runtime. Separate pipe
owners keep the process boundary small and fit Foundation task ownership.
