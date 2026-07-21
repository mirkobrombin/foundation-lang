# Bootstrap

Foundation currently uses one C++20 compiler implementation.

## Stage 0

Stage 0 is written in C++20 and builds with GCC, Clang, or MSVC. It has no third-party library
dependency. It is the product compiler, package tool, and compiler service shared with the
language server. The name records its bootstrap origin; it does not imply a required later stage.
`foundationc build`, `run`, and `test` resolve the configured native C compiler by executable name
through `PATH`; the execution environment must provide the compatible toolchain.

The current executable subset includes typed and generic functions, local bindings, calls,
primitive expressions, generic nominal value structs and enums, exhaustive match expressions,
Option and Result primitives, Result must-use analysis, `const ... else`, fatal panic traces, field
access and mutation, ordered field defaults, plain read parameters, `&` edit loans, `$` transfers,
`new` construction,
deterministic destruction, immutable UTF-8 String
values, fixed arrays, borrowed slices, checked indexing, branches, loops, compile-time contracts,
receiver methods, contract inheritance, default methods, field delegation, borrowed and owned
dynamic dispatch, semantic resolution, typed FIR, all fixed-width and pointer-width machine
scalars, checked integer arithmetic, IEEE floating-point arithmetic, explicit checked numeric
conversion, and the diverging `never` type. Named functions are first-class values, generic function values infer from an expected
signature, and closures use explicit copy, edit, or transfer captures with deterministic
environment destruction. Mutable places support ownership-preserving replacement, complete struct
destructuring moves every field, and compiler-managed struct destructors can prepare values for
automatic field cleanup. The first dynamic collection, `std.collections.List<T>`, is Foundation
source and drains long owner chains iteratively. Stage 0 also supports checked C ABI imports and
exports, deterministic public headers, and native C or object inputs. Each added construct must
serve the stage-1 compiler or an accepted language invariant.
Bootstrap compatibility still accepts the word ownership forms `view`, `edit`, and `own` while
the source corpus migrates. Executables may receive a borrowed command-line String slice through
`fn main(args [String]) i32`; the generated C adapter owns and releases the temporary slice storage.
Package-scope `@target(linux)`, `@target(macos)`, and `@target(windows)` declarations are selected
before linking. The Foundation-source `std.platform` package exposes that choice through a typed
API without C preprocessor conditions in application code.
Package-defined attributes carry typed constant metadata on declarations and members. Stage 0
checks their package visibility, targets, repetition, arguments, and metadata-safe aggregate
types. FIR retains the resolved values, and `emit-metadata` writes the deterministic
`foundation.metadata/v1` manifest without changing emitted C.
Service implementations and method receivers also remain explicit in FIR. `emit-app-plan` uses
that typed model to write `foundation.application/v1`, including construction order, lifetimes,
boundary inputs, contract providers, action signatures, names, keys, and policies. The planner
reports graph errors before application symbols are derived. Binding, validation, DI, actions, and
web host types are synthesized into the compiler semantic model and FIR without writing project
source. The derived application stores
singleton services, reconstructs transient action graphs for each invocation, and exposes typed
action methods without runtime reflection. `FoundationScope` owns scoped services, builds them once
per `NewScope` call, and releases them in reverse dependency order through ordinary value drop.
Fallible constructors share one application error type and make the generated builder return an
explicit `Result`; scope construction uses its own shared typed error when needed. Fallible
transient constructors share one typed error within an action activation. The generated action
wrapper reports activation failure explicitly, preserves an action with the same Result error,
and nests an action Result with a different error instead of erasing either failure boundary.
The host also emits closed request, result, and dispatch-error enums. Direct dispatch stays fully
typed. Name and key lookup validate a dynamic selector against the already typed request variant,
and policy-bearing actions require a `foundation.actions.Authorizer`. Scoped actions use a
separate request enum and explicit scope argument. Dispatch payloads and results cannot retain
borrowed slices or borrowed contract values. Generated action-name and key-binding introspection
uses fixed arrays in deterministic order and performs no runtime registration.
Dispatch remains synchronous. An action can return an owned `Task<T>` created with `spawn`, and
can transfer an explicit `std.concurrent.Cancellation` payload into that task. The generated result
enum preserves the task handle without waiting or detaching it.
The Foundation-source `foundation.events.Bus<T, E>` stores typed handlers, preserves stable
priority order, and returns typed stop-first or best-effort publication failures.
`ConcurrentBus<T, E>` keeps that editable bus inside one pump task, accepts events through cloned
typed publishers with bounded backpressure, and returns ordered handler failures from draining
shutdown. The dispatcher still performs no hidden publication.
Ownership transfers from stored services remain hard errors.
The Foundation-source `std.env` package returns explicit absence and error values while the C
runtime copies and validates process text. Its result never borrows native environment storage.
`std.text` exposes checked byte inspection and UTF-8-boundary slicing. `std.path` builds owned
platform paths in Foundation source using those operations.
`std.fs` adds read-only file size, directory iteration, and line streaming. Foundation reader
structs own opaque runtime handles and close them through custom drop. Line reads accept explicit
limits, consume oversized lines without retaining them, and distinguish invalid UTF-8 from an
oversized input.
`std.net` adds TCP connect, split read and write ownership, bounded UTF-8 line reads, and complete
writes. DNS runs on the blocking executor; socket progress and cancellation use the callback
reactor without exposing native handles to applications.
`std.format`, `std.parse`, `std.json`, and `std.time` supply scalar formatting, integer parsing, owned JSON values,
UTC instants, and fixed calendar formatting in Foundation source. Native code remains limited to
clock access, calendar conversion, byte-level String operations, and platform filesystem handles.
`std.concurrent` supplies ref-counted cancellation sources and tokens. A token observes explicit
source cancellation and structured task cancellation through one API.
Task waits at standalone binding and void-statement positions lower to numbered C11 states. The
runtime removes a waiting parent from the cooperative queue and wakes it when its owned child is
ready; task locals remain in the generated frame across that suspension.
The same internal park and wake contract backs the channel transport. Stage 0 channel queues
support rendezvous, bounded FIFO buffering, independent endpoint closure, cancellation, and owned
payload cleanup. Source-level `send` and `receive` operations lower to numbered task states and
keep pending payload storage in the generated task frame.

Stage 0 bounds parser nesting and expression complexity before semantic analysis. It records at
most 100 specific errors, followed by FDN0000 when more input errors are suppressed. These limits
keep malformed input deterministic under the sanitizer and fuzzing configurations.

`print` is a stage-0 bootstrap intrinsic, not a permanent language primitive. Once streams and
console output can be implemented through the runtime ABI, ordinary programs will use the
standard-library `io.println` operation. Interactive tools may provide a short `print` convenience.
Fatal `panic` remains a language operation because it defines control flow and trace semantics.

## Future implementation work

The compiler may be reimplemented in Foundation after the language and tooling are complete. That
would be a separate compatibility project, not a release gate. It must reuse the conformance suite
and preserve compiler behavior, diagnostics, package metadata, and generated C contracts.

## Package boundary

Official packages are not implemented in stage 0. The first usable subset may contain compiler
builtins for operations that cannot yet be expressed, but every builtin must have:

- a specification entry;
- a stable runtime ABI operation when runtime support is needed;
- a tracked removal or permanent-intrinsic decision;
- conformance tests shared by every supported compiler implementation.

The bootstrap binary currently receives the standard-library source root at its own build time and
adds every sorted `.fn` file to a project. This makes official packages available without copying
them into examples while preserving their Foundation implementation. It is temporary: the product
toolchain must locate its installed standard library and resolve external packages through versioned
manifests and a lock file.
