# `std.terminal`

`std.terminal` opens the process terminal in raw mode for interactive applications.

```foundation
fn Open() Result<own Terminal, Error>
task Read($reader own Reader, limit u64) ReadOutcome
fn Controller.Close(&self) bool
```

`Open` requires terminal-backed standard input and output. It returns the initial window size, a
reader, and an independent controller. `Read` produces input bytes, window size changes, or EOF.
The controller restores the original input and output modes and wakes a blocked read when closed.
Dropping the controller provides the same restoration guarantee.

Only one terminal session may be active in a process. A read is limited to 16 MiB. Interactive
applications normally use a smaller bound and move the reader into a dedicated task while retaining
the controller in the task that owns the session lifecycle.
