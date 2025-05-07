# Bootstrap

Foundation uses three compiler stages.

## Stage 0

Stage 0 is written in C++20 and builds with GCC, Clang, or MSVC. It has no third-party library
dependency. Its job is to implement enough of the language to compile the next compiler. Stage 0
remains available as a recovery compiler after self-hosting.

The current executable subset includes typed functions, local bindings, calls, primitive
expressions, nominal value structs, field access, branches, loops, semantic resolution, typed FIR,
and checked i32 operations. Each added construct must serve the stage-1 compiler or an already
accepted language invariant.

Stage 0 bounds parser nesting and expression complexity before semantic analysis. It records at
most 100 specific errors, followed by FDN0000 when more input errors are suppressed. These limits
keep malformed input deterministic under the sanitizer and fuzzing configurations.

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
