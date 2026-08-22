# `std.env`

`std.env` reads the process environment through the portable runtime boundary. It does not expose
mutation in the current surface.

```foundation
import std.env

const key = "FOUNDATION_HOME"
const configured = env.Get(key) else error {
    return reportEnvironmentError(error)
}
```

The public surface is:

```foundation
enum Error {
    InvalidName
    InvalidUtf8
    Platform
}

fn Get(name String) Result<Option<String>, Error>
fn Home() Result<Option<String>, Error>
```

`Get` distinguishes a missing key from an empty value. It returns an owned UTF-8 copy and never
borrows process storage. Invalid names and non-UTF-8 values are recoverable errors. Allocation
failure follows the language-wide fatal panic rule. `Home` reads `HOME` on Linux and macOS and
`USERPROFILE` on Windows through target-selected Foundation declarations.
