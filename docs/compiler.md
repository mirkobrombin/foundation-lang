# Compiler implementation

Status: current repository implementation for the Foundation 1.0 target. Release state is
maintained in the [README](../README.md#status).

The repository contains a Foundation compiler and its C++20 stage0. The Foundation implementation
owns parsing, package resolution, semantic analysis, FIR, application derivation, C11 output,
LLVM output, and native build, run, and test commands. Stage0 remains available for bootstrap and
the existing formatting, documentation, linting, and language-server entry points.

## Bootstrap

Stage0 emits the stage1 C source. The resulting stage1 compiler emits stage2, then the stage2
compiler emits stage3. A successful bootstrap requires stage2 and stage3 to be byte-identical.
Stage1 is not part of that comparison because it was emitted by the independent stage0
implementation.

```sh
cmake --build --preset dev --target foundation_selfhost_bootstrap
./build/dev/foundationc-selfhost version
```

The target resolves the package for the host, compiles both Foundation generations with warnings
as errors, compares their generated source, and leaves `foundationc-selfhost` as the final
executable. An SDK install includes that executable after the bootstrap target has built it. CI
runs the same bootstrap on Linux, macOS, and Windows.

## Build and output

LLVM emits the default native object. The platform compiler driver links it with the C11 runtime
and the native inputs declared by the package. `--backend c` selects the portable C11 backend, and
`emit-c` writes that source without linking it.

The root README states the LLVM version required by the current checkout. CMake owns exact version
checks for LLVM and optional providers. Provider-specific source revisions belong in their build
configuration and provider README, not in this implementation overview.

OpenSSL and WAMR are optional builds. OpenSSL supplies TLS and asymmetric authentication. WAMR
supplies WebAssembly guest execution through the engine-neutral plugin ABI. Neither provider
changes Foundation IR or the base runtime dependency set.

## Front end and Foundation IR

The compiler discovers project sources in stable path order, parses each file, links the package
graph, resolves names and types, checks ownership, and lowers the program to typed Foundation IR.
Backends consume FIR rather than parser nodes. Diagnostics keep source spans through each phase.

The [language specification](language.md) defines the accepted syntax and semantics. Accepted and
rejected fixtures exercise that boundary through the compiler entry point; this document does not
duplicate an exhaustive feature inventory.

Package-scope `@target(linux)`, `@target(macos)`, and `@target(windows)` declarations are selected
before linking. `check`, `emit-c`, `emit-c-header`, and `emit-metadata` accept an explicit target
that must match the package lock. Native build, run, and test commands remain host-targeted.

Package-defined attributes carry typed constant metadata. The compiler checks visibility, target,
arguments, repetition, and metadata-safe value types. Resolved applications survive in FIR, and
`emit-metadata` writes the `foundation.metadata/v1` manifest without changing native output.

C ABI imports and exports also survive in FIR. `emit-c-header`, `emit-pii`, `build-library`, and
`package export` derive their artifacts from the same specialized interface. Their accepted types
and output formats are defined in the [C ABI section](language.md#c-abi).

## Application model

Services, constructors, actions, contracts, and their typed attributes remain explicit through
semantic analysis and FIR. `emit-app-plan` writes `foundation.application/v1` after validating the
provider graph, lifetimes, boundary inputs, action signatures, names, keys, and policies.

Binding, validation, DI, actions, and web host types are derived inside the compiler model. The
language server reads those symbols directly. `emit-app-host` writes the equivalent Foundation
source for inspection, but a build does not read or require that file.

The derived host stores singleton services, constructs scoped services per scope, and rebuilds
transient action graphs per invocation. Constructor and action failures remain typed `Result`
values. Dynamic name or key dispatch validates a selector against closed request and result enums;
direct dispatch remains statically typed.

## Tasks and runtime boundaries

Task waits lower to numbered states in an owned frame. The runtime removes a waiting task from the
cooperative queue and wakes it when the child, channel, timer, blocking operation, or native
callback becomes ready. The owning executor alone polls and destroys the frame.

The compiler emits calls through the stable C runtime ABI. Standard-library and Foundation
packages implement data-format and application policy in Foundation source. Detailed package
contracts live under [standard library](stdlib/) and [Foundation packages](framework/).

## Diagnostics and limits

Parser nesting and expression complexity are bounded before semantic analysis. One compilation
records at most 100 specific errors, followed by `FDN0000` when further input errors are suppressed.
Sanitizer and fuzzing builds exercise those limits.

Compiler diagnostics are the same across command-line and language-server entry points. Builtins
such as `print` expose signatures, hover text, completion, and definition navigation from the same
compiler model.

## Package boundary

Official standard-library and Foundation packages are Foundation source loaded from the configured
SDK root. A compiler intrinsic is permitted only when the operation cannot be expressed in
Foundation. Each intrinsic requires:

- a specification entry;
- a stable runtime ABI operation when runtime support is needed;
- a compiler-intrinsic classification;
- conformance tests.

Development builds receive fallback SDK roots at build time. Installed compilers locate the SDK
beside the executable or through `FOUNDATION_SDK_ROOT`. External packages resolve through versioned
manifests and the exact target lock.
