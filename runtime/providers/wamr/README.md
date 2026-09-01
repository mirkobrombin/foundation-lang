# WAMR provider

The optional WAMR provider runs Foundation WebAssembly plugins without adding an engine dependency
to the base compiler or runtime. It implements provider ABI 2.0 against WAMR 2.4.5 at revision
`25bd7eb63e828e4bd242cc9b38d260b4b31c6605`.

## Lifetime and concurrency

The application selects the provider library explicitly. Engine and module references carry a
generation, which prevents an old reference from reaching a new object that reused the same slot.
Closing first revokes the public reference, then waits for active work before releasing native
state.

File reads do not hold the global registry lock. WAMR load and unload operations are serialized per
engine, while calls share no execution environment and therefore serialize per module. Separate
modules can still run concurrently.

Metadata evaluation receives no host capabilities. Runtime calls can use only capabilities that
appear in verified metadata and are granted by the application. Process arguments, environment,
and directories are never inherited. Read-only WASI preopens are rejected because this WAMR API
cannot enforce distinct read and write rights; any writable preopen must be supplied explicitly.

## Build and verify

```sh
cmake -S . -B build/wamr -G Ninja \
    -DBUILD_TESTING=ON \
    -DFOUNDATION_BUILD_WAMR_PROVIDER=ON \
    -DFOUNDATION_WAMR_SOURCE=/path/to/wasm-micro-runtime \
    -DFOUNDATION_WAMR_LIBRARY=/path/to/libiwasm.a
cmake --build build/wamr
ctest --test-dir build/wamr --output-on-failure \
    -R '^(runtime\.wamr-bridge|providers\.wamr\.native|compiler\.run\.plugin-wamr-provider-(fake|real))$'
```

`FOUNDATION_WAMR_SANITIZERS=ON` instruments the runtime and provider fixtures with AddressSanitizer
and UndefinedBehaviorSanitizer. Use `FOUNDATION_WAMR_TSAN=ON` in a separate build for
ThreadSanitizer; the options cannot be combined.

The bridge and native tests both force a close while an open is active, checking that engine
destruction waits for the live module. Other fixtures exercise the fake and pinned engines against
invalid metadata, signature and memory failures, denied capabilities, cancellation, stale handles,
and bounded payloads.
