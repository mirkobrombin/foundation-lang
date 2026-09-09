# `std.process`

`std.process` starts child processes without passing arguments through a shell. A `Command` owns
its native configuration and accepts arguments, a working directory, and environment entries.

```foundation
fn Open(program String, $options Options) Result<own Command, Error>
fn Command.AddArgument(&self, argument String) Result<void, Error>
fn Command.AddEnvironment(&self, entry String) Result<void, Error>
fn CurrentID() u64
fn Executable() Result<String, Error>
fn Run(
    program String,
    arguments [String],
    environment [String],
    $options Options
) Result<own Output, Error>
task Start($command own Command) Result<own Child, Error>
task Read($reader own Reader, limit u64) ReadOutcome
task Write($writer own Writer, $value own bytes.Bytes) WriteOutcome
task Wait($waiter own Waiter) Result<i32, Error>
fn Writer.Close(&self) bool
fn Controller.Abort(&self) bool
fn OpenPTY(
    command [String],
    environment [String],
    $options PTYOptions
) Result<own PTYCommand, Error>
fn PreparePTY(
    $command own Command,
    columns u16,
    rows u16
) Result<own PTYCommand, Error>
task StartPTY($prepared own PTYCommand) Result<own PTY, Error>
task ReadPTY($reader own PTYReader, limit u64) PTYReadOutcome
task WritePTY($writer own PTYWriter, $value own bytes.Bytes) PTYWriteOutcome
task WaitPTY($waiter own PTYWaiter) Result<i32, Error>
fn PTYController.Resize(&self, columns u16, rows u16) Result<void, Error>
fn PTYController.Abort(&self) bool
```

`Run` captures stdout and stderr separately and returns the child's exit code. `OutputLimit` caps
their combined size. The default environment is inherited from the parent process. Entries added
with `AddEnvironment` replace inherited entries with the same name; duplicate additions remain an
error. Set `InheritEnvironment` to `false` to start from an empty environment.

`CurrentID` returns the current process identifier. `Executable` returns the path used by the host
system for the running executable.

`Start` launches a configured command with separate pipes for stdin, stdout, and stderr. The child
contains independent owners for each pipe, process control, and process waiting so tasks can use
them without shared mutable Foundation state. `Read` returns one bounded binary chunk or `None` at
EOF. `OutputLimit` applies to `Capture`; streaming callers set the bound on each `Read`. `Write`
writes the complete byte value. Closing the writer closes the child's stdin pipe.
`Wait` returns a normal exit code or `128 + signal` on POSIX. `Abort` terminates the child and wakes
pending stream operations. Dropping the final child owner terminates and reaps a child that has not
exited.

`OpenPTY` prepares an interactive process. An empty command uses `SHELL` on POSIX and `COMSPEC` on
Windows, with a platform fallback when the variable is absent. The default working directory is
the current user's home directory. `TERM` defaults to `xterm-256color`; a caller-provided `TERM`
entry is replaced by `PTYOptions.Term`.

`PreparePTY` accepts a `Command` assembled by another package. It preserves the command's
arguments, environment, and working directory while adding the initial terminal size.

`StartPTY` uses a POSIX pseudo-terminal or Windows ConPTY. It returns four independent owners so a
reader task, writer task, controller, and waiter can be moved into separate scopes without shared
mutable Foundation state. `ReadPTY` returns `None` after terminal EOF. `WritePTY` writes the full
byte value. `WaitPTY` returns a normal exit code or `128 + signal` on POSIX. `Abort` terminates the
child and wakes pending terminal I/O.

The error enum distinguishes invalid input, unavailable platform support, closed handles, resource
limits, and I/O failures. Windows versions without ConPTY return `Unavailable`. Dropping the final
PTY owner terminates and reaps a child that has not exited, then releases every native handle.
