# `std.platform`

`std.platform` exposes the selected application platform without leaking compiler or C
preprocessor conditions into application source.

```foundation
import std.platform

let current = platform.Current()
print(platform.Name(current))
```

The bootstrap surface contains:

```foundation
enum Kind {
    Linux
    MacOS
    Windows
}

fn Current() Kind
fn Name(platform Kind) String
fn IsUnix(platform Kind) bool
```

`Current` is implemented by three Foundation declarations selected with `@target`. It performs no
allocation and returns the target used for compilation. It is not a probe of the machine that
happens to execute a cross-compiled binary.
