# Bootstrap

Foundation uses three compiler stages.

## Stage 0

Stage 0 is written in C++20 and builds with GCC, Clang, or MSVC. It has no third-party library
dependency. Its job is to implement enough of the language to compile the next compiler. Stage 0
remains available as a recovery compiler after self-hosting.

The current executable subset includes typed and generic functions, local bindings, calls,
primitive expressions, generic nominal value structs and enums, exhaustive match expressions,
Option and Result primitives, Result must-use analysis, `let ... else`, fatal panic traces, field
access and mutation, `own`, `view`, `edit`, deterministic destruction, immutable UTF-8 String
values, fixed arrays, borrowed slices, checked indexing, branches, loops, compile-time contracts,
receiver methods, borrowed dynamic dispatch, semantic resolution, typed FIR, and checked i32
operations. Named functions are first-class values, generic function values infer from an expected
signature, and closures use explicit copy, own, view, or edit captures with deterministic
environment destruction. Mutable places support ownership-preserving replacement, complete struct
destructuring moves every field, and compiler-managed struct destructors can prepare values for
automatic field cleanup. The first dynamic collection, `std.collections.List<T>`, is Foundation
source and drains long owner chains iteratively. Stage 0 also supports checked C ABI imports and
exports, deterministic public headers, and native C or object inputs. Each added construct must
serve the stage-1 compiler or an accepted language invariant.

Stage 0 bounds parser nesting and expression complexity before semantic analysis. It records at
most 100 specific errors, followed by FDN0000 when more input errors are suppressed. These limits
keep malformed input deterministic under the sanitizer and fuzzing configurations.

`print` is a stage-0 bootstrap intrinsic, not a permanent language primitive. Once streams and
console output can be implemented through the runtime ABI, ordinary programs will use the
standard-library `io.println` operation. Interactive tools may provide a short `print` convenience.
Fatal `panic` remains a language operation because it defines control flow and trace semantics.

## Stage 1

Stage 1 is the compiler written in Foundation Lang and compiled by stage 0. It must pass the same
conformance suite and emit the same canonical Foundation IR for the bootstrap corpus.

## Stage 2

Stage 2 is stage 1 compiled by stage 1. Bootstrap is accepted only when stage 1 and stage 2 produce
matching normalized artifacts on three consecutive clean builds.

The comparison includes compiler behavior, diagnostics, package metadata, and generated C. Binary
identity is required only after platform paths, timestamps, and native toolchain metadata have been
removed or fixed by specification.

## Package boundary

Official packages are not implemented in stage 0. The first usable subset may contain compiler
builtins for operations that cannot yet be expressed, but every builtin must have:

- a specification entry;
- a stable runtime ABI operation when runtime support is needed;
- a tracked removal or permanent-intrinsic decision;
- conformance tests shared by stage 0 and the self-hosted compiler.

The bootstrap binary currently receives the standard-library source root at its own build time and
adds every sorted `.fdn` file to a project. This makes official packages available without copying
them into examples while preserving their Foundation implementation. It is temporary: the product
toolchain must locate its installed standard library and resolve external packages through versioned
manifests and a lock file.
