# `std.system`

`std.system` reports stable information about the current process host without exposing native
platform APIs.

```foundation
import std.system

const info = system.Snapshot() else return 1
print(info.Hostname + " " + info.Architecture)
```

`Snapshot` returns the hostname, the architecture used by the current process, and the number of
logical processors available to it. Hostname decoding failures remain explicit.
