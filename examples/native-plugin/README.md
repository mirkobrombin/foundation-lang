# Native plugin example

This project loads a shared library through `foundation.plugin`. Compatibility is checked before
the plugin creates state; after that point the plugin uses the Foundation allocator, owns one
opaque context, and participates in an explicit start, stop, and destroy lifecycle.

Build the plugin from the repository root:

```sh
cmake -S examples/native-plugin/native -B build/native-plugin
cmake --build build/native-plugin
```

Run the Foundation application on Linux:

```sh
./build/dev/foundationc run examples/native-plugin -- \
    build/native-plugin/libgreeter_native.so
```

Use `libgreeter_native.dylib` on macOS and `greeter_native.dll` on Windows. A plugin written in C,
C++, Zig, Rust, or Go can export the same `foundation/plugin.h` contract as long as it keeps foreign
runtime objects behind the C ABI.

The checked-in lock targets Linux. Before building the example on another host, generate the lock
for that host:

```sh
foundationc package resolve examples/native-plugin --target macos
foundationc package resolve examples/native-plugin --target windows
```

Run only the command for the current host. Package resolution replaces `foundation.lock` with a
lock for that target, preventing the project from reusing a lock produced for another platform.
