# Architecture

Status: current repository implementation for the Foundation 1.0 target. Release state is
maintained in the [README](../README.md#status).

## Compilation path

```text
.fn project files
    -> deterministic discovery
    -> lexer and parser per source
    -> target declaration selection
    -> package graph and source-aware syntax trees
    -> import linking and visibility checks
    -> name and type analysis
    -> typed high-level representation
    -> Foundation IR
       |-> deterministic typed metadata manifest
       -> ownership and control-flow lowering
       |-> LLVM IR and native object
       |-> C11 source and native C compiler
       -> native linker and Foundation runtime
```

Native library packages add a sibling artifact path after FIR specialization:

```text
Checked project + target lock
    -> specialized Foundation IR
    -> Package Interface IR
       |-> C header and native library
       |-> Zig module
       |-> Rust crate
       |-> Go cgo or dynamic package
       -> translated Go source package
```

The artifact generators produce static and shared C ABI bundles from the same specialized FIR and
Package Interface IR. Each bundle contains the native library, public header, C ABI support header,
and canonical interface metadata. LLVM is the default object producer and C11 remains an explicit
backend. Zig, Rust, `go-cgo`, and `go-dynamic` consume that checked C contract.

`go-source` is a separate FIR translation path and emits no native artifact. It specializes
reachable generic code before translating the accepted value and control-flow subset. Unsupported
ownership, ABI, or runtime forms fail with `FDN4120`; the exact accepted and rejected surface is
defined in the [language specification](language.md#c-abi).

Each phase owns a typed input and output. Diagnostics retain source spans across lowering. Native
backends consume Foundation IR and cannot inspect parser nodes.

Concrete receiver calls lower to ordinary FIR function calls. Contract inheritance is flattened in
the semantic model before FIR, so each specialized contract has one deterministic slot sequence.
A borrowed contract conversion adds a data pointer and a typed vtable in FIR. Default and delegated
slots carry explicit adapter targets and field paths. The C backend emits one static adapter table
for each reachable contract and concrete-type pair; borrowed conversion itself does not allocate.

An owned contract conversion retains the owned concrete pointer in an allocated existential
wrapper. The same vtable includes concrete destruction glue. Moving the wrapper clears its source,
borrowing it copies the dynamic view without allocation, and cleanup calls the concrete drop path
before releasing both allocations. No adapter discovers behavior through runtime reflection.

Package-defined attribute schemas and resolved applications survive in FIR. The metadata emitter
serializes them independently of the C backend under a versioned schema. Attribute values cannot
execute code and do not participate in reachability, object layout, ownership lowering, or runtime
dispatch. `@target` is removed earlier and never enters this metadata path.

C ABI declarations also survive into FIR. Bodyless declarations lower to internal adapters that
enter a native trace frame and call the named C symbol. Definitions retain their normal internal
Foundation function and add a public C wrapper. The public header is generated from the same
specialized FIR as the C translation unit, so header and implementation cannot use separate type
models.

A function value lowers to a typed C value containing an environment pointer, a call pointer, and
a drop pointer. Named functions use generated adapters with no environment. Each closure receives
a generated environment struct, call thunk, and drop function. Capture ownership is explicit in
FIR, so the backend does not infer lifetimes or hidden copies. `transferable fn` remains distinct
through semantic analysis and FIR validation, then lowers to the same C11 representation as an
ordinary function value.

A direct `extern c fn` pointer is a separate FIR type. It contains only the native call pointer,
has no environment or drop path, and retains the C calling convention through C11 and LLVM
lowering. PII 1.2 can place it in checked nominal C layouts or expose it directly at a C boundary.

Place replacement and consuming struct destructuring remain explicit FIR nodes. Replacement emits
the replacement computation before one place computation, then two ownership-aware moves.
Destructuring moves every field to a new local and drops the cleared source; an owned source also
releases its outer allocation. A struct may name a specialized Foundation `drop` method in FIR.
Generated struct cleanup guards it with a private moved-state bit, calls it once, then performs
automatic reverse-order field cleanup.

Project compilation assigns every source a stable ID after sorting its normalized relative path.
The parser validates compiler-known target attributes and removes inactive declarations before
the package symbol table is built. Inactive bodies leave no expressions, blocks, closures, or
methods in the linked program.
The package linker rejects missing packages, alias collisions, private access, ambiguous entry
points, and cycles before semantic analysis. The compiler combines the linked graph into one FIR
program. The C11 backend emits one translation unit.

## Layers

`compiler` is the product compiler implementation. It may use C++20 standard-library facilities but cannot
leak a C++ type into emitted code, runtime headers, package metadata, or the language ABI.

`runtime` contains the stable C ABI and platform shims. It exposes capability, not policy. Memory,
threads, clocks, files, sockets, and dynamic loading enter the language through this boundary.
Native inputs compile beside generated C and include the generated `foundation_abi.h`; no compiler
or C++ type crosses that header.

Native plugins use the separate versioned `foundation/plugin.h` table. Dynamic loading resolves one
query symbol, validates ABI, SDK, target, contract hash, UTF-8 data, and callback presence, then
creates the opaque plugin context. The Foundation framework owns lifecycle order and rollback;
the runtime owns only table validation, library handles, allocator exchange, and callback entry.

External process plugins use a separate runtime adapter. It builds an argument vector without a
shell, owns stdin and stdout control pipes, bounds the JSONL ready line, applies monotonic start and
stop deadlines, and reaps the child on every exit path. Foundation source owns the typed factory
registry, JSON validation, task-facing outcomes, and error policy.

WebAssembly plugins use a separate guest ABI in `foundation/wasm_plugin.h`. Foundation source owns
metadata validation, named grants, and capability authorization. The optional WAMR adapter
owns module compilation, memory access, calls, deadlines, and explicit WASI resources. The adapter
does not expose engine types through the runtime ABI or make undeclared host resources available
to a guest.
Engine-neutral discovery enumerates non-directory `.wasm` paths from one directory, filters without
opening modules, and sorts the owned results before an adapter sees them.

The runtime multiplexes timer, blocking-worker, and native-callback completion sources before a
task is polled. A portable reactor token lets C ABI adapters publish completion from foreign
threads without entering the Foundation scheduler. The owning executor alone drains the queue,
wakes the task, and consumes the token. Platform adapters implement the actual file, socket, GUI,
or foreign-runtime request; the core reactor does not encode Unix descriptors or Windows handles.

`std` contains portable language packages. `foundation` contains application packages. Both are
compiled like user code and must not receive compiler-only privileges except through specified
language attributes.

The Foundation SDK ships the compiler, runtime, standard library, and Foundation framework as one
coordinated product. This distribution rule does not erase the dependency boundary: applications
import universal facilities through `std.*` and application architecture through `foundation.*`.
DI, plugin lifecycle, adapters, routing, state machines, scheduling, and hosting belong to the
framework. Contracts, typed attributes, compile-time metadata, FFI, and portable dynamic loading
are the language or standard-library mechanisms on which those packages build.

Portable JSON parsing, collection ownership, time values, formatting, filesystem policy, and TCP
stream policy live in Foundation source. The C runtime supplies byte operations,
allocation-backed String building, native clocks, filesystem handles, DNS resolution, and socket
completion. This keeps data-format and stream policy out of the compiler and platform branches out
of application packages.

The compiler loads application dependencies through `foundation.package`, the exact target lock,
and the verified content-addressed cache. It also adds all `.fn` files under the standard-library
and Foundation framework roots configured by the SDK. Reachability specialization prevents unused
package code from entering native output.

`tools` consumes public compiler services. A formatter or language server cannot maintain a second
parser or type system.

## Dependency direction

```text
tools -> compiler services -> syntax / semantic / FIR
                                     |
                                     v
                              native backends

foundation packages -> standard library -> runtime ABI
user packages       -> standard library -> runtime ABI
```

The runtime cannot depend on the compiler, standard library, or Foundation framework. Standard
packages cannot depend on Foundation packages.
