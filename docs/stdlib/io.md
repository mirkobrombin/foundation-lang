# Standard input and output

`std.io` reads and writes process standard streams.

`WriteStdoutText` and `WriteStderrText` write UTF-8 text without adding a line
terminator. `WriteStdoutBytes` and `WriteStderrBytes` write arbitrary bytes.

`ReadStdinLine` reads one UTF-8 line without its line terminator. It returns
`None` after end of input and rejects lines larger than 1 MiB.
