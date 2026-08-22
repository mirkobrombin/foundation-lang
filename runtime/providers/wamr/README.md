# WAMR provider

The optional WAMR provider executes Foundation WebAssembly plugins without adding an engine
dependency to the base compiler or runtime. The provider ABI is version 2.0 and the implementation
pins WAMR 2.4.5 at revision `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`.

The application selects the provider library explicitly. Engine and module references include a
generation, so a stale reference cannot reach a newly allocated object. Engine close revokes the
public reference before waiting for active module opens. Module close revokes its reference before
waiting for calls, metadata reads, or configuration operations. Provider close consumes the native
handle after its dependants have quiesced.

Module files are read without the global registry lock. WAMR load and unload operations are
serialized per engine. Calls on one module are serialized because a WAMR execution environment is
not shared concurrently. Different modules may execute concurrently. Metadata evaluation has no
host capabilities, and runtime calls can reach only capabilities declared by verified metadata and
granted by the application.

Read-only WASI preopens are rejected because the selected WAMR interface cannot enforce distinct
read and write rights. Writable preopens, environment entries, and arguments must be supplied
explicitly. Process environment, arguments, and directories are never inherited.

## Build and verify

```sh
cmake -S . -B build/wamr -G Ninja \
    -DBUILD_TESTING=ON \
    -DFOUNDATION_BUILD_WAMR_PROVIDER=ON \
    -DFOUNDATION_WAMR_SOURCE=/path/to/wasm-micro-runtime \
    -DFOUNDATION_WAMR_LIBRARY=/path/to/libiwasm.a
cmake --build build/wamr
ctest --test-dir build/wamr --output-on-failure \
    -R '^(runtime\.wamr-bridge|providers\.wamr\.native|stage0\.run\.plugin-wamr-provider-(fake|real))$'
```

`FOUNDATION_WAMR_SANITIZERS=ON` instruments the Foundation runtime, provider, fake provider, and
native concurrency tests with AddressSanitizer and UndefinedBehaviorSanitizer.
`FOUNDATION_WAMR_TSAN=ON` is a separate ThreadSanitizer build. The two options cannot be combined.

`runtime.wamr-bridge` forces close during an active open through the public runtime bridge and
checks deferred engine destruction with a live module. `providers.wamr.native` forces the same race
inside the real provider. The Foundation fixtures cover the fake provider, the pinned real engine,
valid metadata, bad signatures, invalid metadata, missing memory, capability denial, cancellation,
stale handles, and bounded payloads.
