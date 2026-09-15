# 0014: Freestanding target

Status: proposed

## User problem

Firmware, kernels, boot code, WebAssembly modules without WASI, and hosts that own every
allocation cannot use Foundation. Every executable and every `build-library` bundle compiles all
hosted runtime sources and links the C library, threads, and platform system libraries. Generated
code always targets the host triple and executables always receive a hosted `int main`.
`--target` selects source and locks, but it never changes code generation. A package that needs only
checked arithmetic, ownership, Strings, and C ABI exports has no supported way to run without an
operating system.

## Design

### Scope

The freestanding target builds a static library whose only environment requirements are five
link-time hooks and the memory primitives every C compiler may call. It is opt-in through the
target. A program that does not select `freestanding` compiles, links, and behaves exactly as it
does today.

Non-goals:

- executables, `main`, startup code, linker scripts, or shared libraries;
- tasks, channels, the reactor, blocking and callback imports, clocks, randomness, files, and
  processes;
- `build`, `run`, and `test` for freestanding code, plugin ABI changes, and `package export`.

### Targets and selectors

`TargetPlatform` gains `Freestanding`, spelled `freestanding`. The selector `hosted` matches
`linux`, `macos`, and `windows`. It is a selector, not a platform.

| Spelling | `@target(...)` | Manifest `target` qualifier | Lock `target` | `--target` |
| --- | --- | --- | --- | --- |
| `linux`, `macos`, `windows` | yes | yes | yes | yes |
| `freestanding` | yes | yes | yes | yes |
| `hosted` | yes | yes | no | no |

A manifest qualifier applies to `dependency`, `native_source`, and `native_link`. Resolving for
`freestanding` activates entries qualified `freestanding` and unqualified entries. Resolving for a
hosted platform activates entries qualified with that platform, `hosted`, or nothing.
`foundationc package resolve <project> --target freestanding` writes `target freestanding` to the
lock. The lock does not record the triple, CPU, features, or C compiler because none of them
affects source selection or resolution.

A `methods` block now accepts `@target` and no other attribute. An inactive block is removed with
its members, like any other inactive declaration.

```foundation
@target(hosted)
methods UUID {
    fn NewV4() UUID {
        var high u64 = 0
        var low u64 = 0
        uuidV4Native(&high, &low)
        UUID { high = high low = low }
    }
}

@target(freestanding)
fn diagnostic(message String) void {
    print(message)
}
```

The following is rejected. The first line reports `FDN4022` in a lock, and `--target hosted`
prints command usage.

```text
target hosted
```

### Commands

`check`, `documentation`, `emit-c`, `emit-c-header`, `emit-metadata`, and the `package` commands
that accept `--target` also accept `--target freestanding`. The selected target must match the
lock, as it does today. None of them requires `main`.

```text
foundationc build-library <project> -o <directory> --kind static --target freestanding
    --triple <llvm-triple> [--cpu <name>] [--features <list>] [--cc <clang>]
    [--backend <llvm|c>] [--pic]
foundationc emit-pii <project> -o <file.json> --triple <llvm-triple> [--cpu <name>]
    [--features <list>]
```

`build-library` enforces these rules. Each violation exits with status 2 and a `foundationc:` usage
message:

- each option appears at most once;
- `--target freestanding` requires `--triple`, and `--triple`, `--cpu`, `--features`, and `--cc`
  require `--target freestanding`;
- the lock target must be `freestanding`; a freestanding lock without `--target freestanding` is
  rejected in the same way as a host mismatch today;
- `--kind shared` is rejected.

`emit-pii` requires `--triple` exactly when the lock target is `freestanding` and accepts `--cpu`
and `--features` only in that case. It validates them in the same way as `build-library`. It has no
`--cc` option because PII does not record the C compiler. `build`, `run`, `test`, and
`package export` reject a freestanding lock through the existing lock-target checks and the export
target check.

The triple is normalized with `llvm::Triple::normalize`. LLVM target lookup failure reports
`FDN8002`. A triple whose data layout has a pointer width other than 32 or 64 bits reports
`FDN8005`.

### CPU and features

`--cpu <name>` selects the LLVM CPU. Without it, the CPU is `generic`, which is not validated.
The compiler validates an explicit name with `MCSubtargetInfo::isCPUStringValid` for the
normalized triple. An unknown name reports `FDN8006`.

`--features <list>` uses LLVM feature string syntax, for example `+fp-armv8,-neon`. The list
contains one or more comma-separated entries without whitespace. Each entry is `+` or `-` followed
by a name that matches `[A-Za-z0-9][A-Za-z0-9._-]*`. The compiler reports `FDN8007` in these
cases:

- an empty list, an empty entry, or an entry without a sign;
- a name that appears more than once;
- a name that is not in the subtarget's `getAllProcessorFeatures()` for the triple and CPU.

LLVM itself only warns about unknown features, so the compiler checks this before code generation.

The compiler sorts the entries by name. The sorted list is the effective feature string: the order
on the command line has no meaning. The LLVM backend passes the CPU and the sorted string to
`createTargetMachine`. When `--cpu` is present, every C compilation receives
`-Xclang -target-cpu -Xclang <cpu>`. Without it, C compilations keep the Clang default CPU for the
triple, because Clang rejects the name `generic` for some triples, such as x86. Every C compilation
receives one `-Xclang -target-feature -Xclang <entry>` pair per entry in sorted order. These options
come after every option the driver derives from the triple. The Clang driver may add default
features for the triple, for example a default RISC-V ISA. Explicit entries take precedence over
those defaults because the compiler front end applies features in order.

The CPU and features affect the archive and PII, never the lock or source selection.

### C compiler

Without `--cc`, the configured C compiler is used and must have the build-time compiler ID Clang or
AppleClang. With `--cc <clang>`, the named executable is used instead, so toolchains configured
with GCC or MSVC can build freestanding libraries. The path is resolved like any other program
path and never appears in any output.

The compiler identifies the selected executable by running it twice, without a shell:

1. `<cc> --version` must exit with status 0. The first line of standard output must contain a
   match for `clang version ([0-9]+(\.[0-9]+)*)`. The captured number is the Clang version. The
   prefix `Apple` and vendor prefixes are accepted.
2. A probe compiles a C translation unit that declares one type, because `-Wpedantic -Werror`
   rejects an empty one, with the complete freestanding option set for the triple, CPU, and
   features. It must exit with status 0. This rejects `clang-cl` and Clang builds that lack the
   target.

A failure of either step reports `FDN8008`, naming the executable and the step that failed. A
configured compiler that is not Clang or AppleClang reports `FDN8008` when `--cc` is absent.

### Bundle

The bundle contains `include/<native_name>.h`, `include/foundation/library.h`,
`include/foundation/freestanding.h`, `lib/lib<native_name>.a` on every host, and canonical PII under
`share/foundation/`. The compiler writes the archive itself with the LLVM archive writer in GNU
format with a symbol table and deterministic member headers. Its members, in order, are the
generated object, `core`, `core_print`, and the package's native C and object inputs.

C inputs are compiled with `--target=<triple> -std=c11 -ffreestanding -nostdlibinc -O2 -Wall
-Wextra -Wpedantic -Werror -ffunction-sections -fdata-sections -DFOUNDATION_FREESTANDING=1`,
followed by the CPU and feature options above, the existing source path maps, and `-fPIC` when
`--pic` is present. Native C sources in a freestanding package may include only compiler-provided
freestanding headers and Foundation headers.

### Hook ABI

`foundation/freestanding.h` includes `foundation/library.h` and declares the hook contract.
`FOUNDATION_LIBRARY_ABI_MINOR` becomes 1.

```c
typedef struct fdn_context {
    void *fdn_reserved[8];
} fdn_context;

#define FDN_CONTEXT_INIT {{0}}

typedef struct fdn_panic_location {
    const char *package_name;
    const char *function_name;
    const char *source_file;
    uint32_t line;
    uint32_t column;
} fdn_panic_location;

void fdn_context_init(fdn_context *context);

fdn_context *fdn_hook_context(void);
void *fdn_hook_alloc(size_t size);
void fdn_hook_free(void *value);
_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location);
void fdn_hook_write(const char *data, size_t length);
```

The header uses `[[noreturn]]` under C++ and carries the same linkage guards as `library.h`. The
integrator defines the hooks in C or another language that exports the C ABI. The `fdn_` namespace
is reserved for the runtime, and Foundation `extern c` symbols cannot use it. Foundation source
therefore cannot implement a hook, which prevents the allocator from recursing into Foundation.

Every hook except `fdn_hook_panic` may be called concurrently from different contexts. Each must
be safe in every context that runs Foundation code, including interrupt handlers. No hook may
call a Foundation export or an `fdn_` function, except that `fdn_hook_panic` may call
`fdn_context_init` for another context.

`fdn_hook_context` returns the context of the calling execution. It is required. The runtime calls
it on every Foundation function entry and exit, native boundary entry, and panic, so it must be
constant-time and must not block or allocate. The next section defines its rules.

`fdn_hook_alloc` receives a size of at least 1. The runtime maps a zero-size request to 1, as
the hosted allocator does. The hook returns uninitialized storage aligned to at least
`_Alignof(max_align_t)` for the triple, or `NULL` when storage is exhausted. The runtime turns
`NULL` into the panic `allocation failed`. It owns the storage until it passes the pointer to
`fdn_hook_free`.

`fdn_hook_free` receives only non-null pointers previously returned by `fdn_hook_alloc`, each
exactly once. The call may come from a context other than the one that allocated the pointer. It
cannot fail.

`fdn_hook_panic` is entered once per panic, on the panicking context. `message` is borrowed: its
bytes are not necessarily NUL-terminated and the hook must not release them. `location` describes
the innermost active frame of that context, or is `NULL` when no frame is active. Its strings are
static and NUL-terminated. `package_name` is `NULL` for a native boundary frame.

The hook must not return. It may halt, reset, trap, or transfer control away permanently, for
example with `longjmp` or a scheduler switch, as long as the panicked frames are never resumed.
Drops do not run, and storage owned by those frames is leaked. The panicked context must not run
Foundation code again until it is reinitialized with `fdn_context_init`. Other contexts may
continue. If the hook returns, the runtime executes `__builtin_trap()`.

`fdn_hook_write` is required only when an emitted function calls `print`. For each `print`, it
receives the text bytes, then a single `\n`. The text call is omitted when the String is empty.
The bytes are borrowed for the call. Write failures are ignored, as they are for hosted `print`.
Output from concurrent contexts may interleave between those two calls.

`fdn_alloc`, `fdn_dealloc`, `fdn_string_drop`, `fdn_panic`, and `fdn_panic_cstr` from
`library.h` remain available to native C code and route through the hooks.

A freestanding archive may leave only these symbols undefined:

- the five hooks, with `fdn_hook_write` referenced only by the `core_print` member;
- `memcpy`, `memmove`, `memset`, and `memcmp`, which C compilers may emit in freestanding mode;
- bodyless `extern c` imports declared by the package graph;
- compiler support routines required by the triple's code generator, such as
  `__aeabi_uldivmod` on 32-bit Arm. This includes the `__atomic_` routines on triples without
  lock-free pointer-width atomics.

### Execution contexts

A context is one logical execution that runs Foundation code, such as a core, an RTOS task, or an
interrupt level. `fdn_context` is integrator-owned storage. On every triple, it has the size of
eight pointers and the alignment of a pointer. Its contents are private to the runtime. The
all-zero value is the initial state, written by `FDN_CONTEXT_INIT` for static storage or by
`fdn_context_init` for any other storage. A context holds the head of its frame chain. The
remaining words are reserved for per-context runtime state added by later minors, and zero must
remain their initial value.

These rules apply:

- `fdn_hook_context` returns the same pointer for the whole lifetime of a context. A Foundation
  call that starts on a context completes on that context.
- A context is initialized before first use. It is reinitialized only when no Foundation frame is
  active on it, or after a panic on it.
- Different contexts may run concurrently on different cores.
- An interrupt handler that can preempt Foundation code and calls Foundation code must use a
  different context from the code it preempts.
- Nested re-entry on the same context is valid, for example Foundation calling a native import
  that calls a Foundation export, because the frame chain is a stack.
- A native import may switch the current context before calling a Foundation export and restore
  it before returning. The export then runs on the new context with its own frame chain.
- Ownership of a value passed between contexts through native code is the integrator's
  responsibility. The runtime synchronizes only its own counters.

The allocation and handle counters are global, not per context, because owned values may be
released on a different context than the one that allocated them. Per-context counts would report
false underflows. The counters use C11 `<stdatomic.h>` operations with `memory_order_relaxed`,
because contexts may run concurrently and the counters order no other memory. Freestanding handle
counters are pointer-width, so they stay lock-free wherever pointer-width atomics are. On triples
without lock-free atomics, the compiler lowers these operations to the `__atomic_` support routines
listed above. Single-threaded WebAssembly lowers them to plain operations.

Thread-local storage is not used, because bare-metal targets lack the TLS runtime that
`_Thread_local` requires.

### Runtime core

The core moves out of `runtime/src/runtime.c` into `runtime/src/core.c`. It contains:

- frames, panic, invalid enum tags, and `fdn_context_init`;
- String move, drop, concatenation, and equality, plus the `fdn_abi_` String and panic adapters;
- bounds checks, checked arithmetic, and UTF-8 validation;
- `fdn_alloc`, `fdn_dealloc`, the allocation counters, and the runtime handle counters;
- the `std.text` natives: the `foundation_runtime_string_*` operations, the String builder, and
  `foundation_runtime_string_hash_fnv1a`.

`runtime/src/core_print.c` holds the freestanding `fdn_println` and `fdn_abi_println`. A separate
archive member keeps the write hook optional.

`runtime/include/foundation/runtime_core.h` declares the core. `runtime.h` includes it and keeps
every other declaration, so hosted generated code sees the same declarations. Generated C for
`freestanding` includes only `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, and
`foundation/runtime_core.h`. The triple must satisfy the IEEE `f32` and `f64` static assertions in
the core header. A violation is a C compilation failure.

Without `FOUNDATION_FREESTANDING`, `core.c` keeps the current hosted bodies unchanged: the
thread-local frame chain, atomic counters, `malloc` and `free`, the stderr trace, and `_Exit`.
Hosted `fdn_println` stays in `runtime.c`. With `FOUNDATION_FREESTANDING=1`:

- the frame chain lives in the context returned by `fdn_hook_context`;
- the counters are the global atomics described above;
- allocation and panic call the hooks;
- `core_print.c` defines printing.

In both modes, arithmetic panic messages are built by string-literal concatenation instead of
`snprintf`, and they keep the current text. The core performs the same `fdn_alloc` and
`fdn_dealloc` sequence in both modes.

### Checked source

The front end checks every package in the graph after `@target` selection. With the
`freestanding` target it rejects:

| Construct | Code |
| --- | --- |
| `task` declaration, `spawn`, `Task` operation | `FDN2186` |
| channel construction, channel operation, `select` | `FDN2187` |
| `@blocking` or `@callback` declaration | `FDN2188` |
| workflow `retry` policy that permits more than one attempt | `FDN2189` |
| import of an SDK package outside the freestanding set | `FDN3011` |
| `main` declaration | `FDN3012` |

If application derivation would emit one of those operations, the compiler reports its code at the
declaration that introduced it. Services, actions, state machines, pipelines, and sagas without
`retry` remain valid when their derived code contains none.

The freestanding SDK set is `std.prelude`, `std.text`, `std.align`, `std.path`, `std.pattern`, and
`std.json`. Every other `std` package and every `foundation` package is hosted. Under
`freestanding`, SDK packages outside the set leave the package graph, so their platform
declarations and natives are never linked, and `std.path` provides `@target(freestanding)`
declarations with the POSIX conventions of `linux` and `macos`. The set is a compiler table shared
by the stage0 and self-hosted compilers. It grows only through a proposal that moves the package's
natives into the core. The prelude moves `UUID.NewV4`, `UUID.NewV7`, and their two natives behind
`@target(hosted)`. Under `freestanding`, calls to them report the existing unknown associated
function diagnostic.

The front end does not know the triple, so `usize` and `isize` use portable bounds. An integer
literal of either type must fit 32 bits, and a violation reports `FDN2005`. A numeric conversion
involving either type is infallible only when it is infallible at both 32 and 64 bits. Runtime
arithmetic and `sizeOf` use the triple's width.

### Code generation

Freestanding compilation has no entry point. It never synthesizes `main`, test runners, or
allocation verification. The roots are the package's `extern c` functions with bodies.
Uppercase exports enter PII and headers. Lowercase exports remain package-private native entries.

The C backend emits the freestanding preamble described above and the package emission used by
`build-library` today. The LLVM backend creates its target machine from the following:

- the normalized triple;
- the selected CPU and sorted feature string;
- PIC relocation for `--pic` and static relocation otherwise;
- function and data sections.

It takes the data layout from that machine and declares only core runtime functions. Both backends
export the same symbol set for the same package.

PII documents for the `freestanding` target declare `abi_minor` 4 and these fields:

- `"target":"freestanding"`;
- the normalized `"triple"`;
- `"cpu"`, which is the selected CPU or `generic`;
- `"features"`, the array of sorted feature entries, empty when `--features` is absent;
- `"freestanding_hooks"`, the sorted list of required hooks. `fdn_hook_write` is listed only when
  an emitted function calls `print`.

Hosted PII keeps its current `abi_minor` and bytes.

### Self-hosted compiler

The self-hosted compiler provides `check`, `emit-c`, `emit-llvm`, `build`, `run`, `test`, and
`package`. It gains the `freestanding` target and the `hosted` selector in `target.fn`, `driver.fn`,
the parser, the manifest and lock models, and the resolver. It also gains `@target` on `methods`
blocks, the freestanding SDK table, every rejection above with the same code and span, and
`emit-c --target freestanding`. Self-hosted `emit-llvm` remains host-only, and `build-library`
remains stage0-only because the self-hosted compiler has no library command.

### Determinism

A freestanding lock has the same bytes on every host. The toolchain identity of a freestanding
build is the `foundationc` build, including its LLVM, plus the Clang version captured during
identification. Two `build-library` runs produce byte-identical headers, archive, and PII when
all of these match:

- the sources and lock;
- the toolchain identity;
- the triple, CPU, features, and other options.

The archive writer zeroes timestamps and owners and writes mode 0644 for every member, so extracted
members stay readable, and objects carry only mapped source paths. A different Clang version may
change the archive bytes but never the headers or PII. The `--cc` path, the host platform, and the
configured host compiler do not affect output.

## Compatibility

A Language 1 program that does not select `freestanding` keeps its meaning. `@target(freestanding)`
and `@target(hosted)` previously reported `FDN1142`, so no valid program changes behavior. Both
compilers already accepted `@target` on a `methods` block; this proposal specifies that form. On
`linux`, `macos`, and `windows`, `UUID` keeps the same method set and behavior. Every new
diagnostic applies only to the `freestanding` target or to the new `build-library` and `emit-pii`
options.

`foundation.package/v1` and `foundation.lock/v1` keep their format identifiers. The new target and
selector values are additive. Existing manifests, locks, and package digests are unchanged. A
manifest or lock that uses the new values requires a toolchain that implements this proposal.

Library ABI 1 hosted bundles, PII minor 3 and earlier, and plugin ABI 1 artifacts remain valid.
The LLVM backend now returns an exported `String` through the C ABI result pointer that the
generated header already declares. Earlier LLVM-built libraries returned it in registers, which C
callers could not read, and must be rebuilt.
Every hosted runtime entry point keeps its name, signature, and behavior. The runtime split only
moves definitions between translation units. Hosted artifacts neither define nor reference
`fdn_hook_` symbols, and they neither define nor use `fdn_context_init`.

The following become part of library ABI 1:

- the five hooks;
- `fdn_context`, with its size, alignment, and all-zero initial state;
- `FDN_CONTEXT_INIT` and `fdn_context_init`;
- `fdn_panic_location`;
- the permitted undefined-symbol set.

A later toolchain keeps them. A later minor may give meaning to reserved context words only if
zero stays their initial value. It may add a new hook only as an optional requirement. An
incompatible change to a hook or to the context layout uses new symbols. The README sentence that
excludes freestanding targets is replaced with the supported contract.

## Diagnostics

- `FDN2186`: a task declaration, `spawn`, or `Task` operation under `freestanding`.
- `FDN2187`: channel construction, a channel operation, or `select` under `freestanding`.
- `FDN2188`: an `@blocking` or `@callback` declaration under `freestanding`.
- `FDN2189`: a workflow `retry` policy that permits more than one attempt under `freestanding`.
- `FDN3011`: an import of an SDK package outside the freestanding set.
- `FDN3012`: a `main` declaration under `freestanding`.
- `FDN8005`: a freestanding triple whose pointer width is neither 32 nor 64 bits.
- `FDN8006`: an explicit `--cpu` that the triple's LLVM target does not recognize.
- `FDN8007`: a malformed `--features` list, a duplicate feature name, or a feature that the
  triple's LLVM target does not recognize.
- `FDN8008`: a selected C compiler that fails Clang identification or the freestanding probe.

`FDN1142` no longer reports `freestanding` or `hosted`. `FDN1160` continues to report any other
attribute on a `methods` block. `FDN4022` also accepts `target freestanding` and rejects
`target hosted`. Manifest target diagnostics accept both new spellings. `FDN2005` applies the
portable `usize` and `isize` bounds under `freestanding`.

## Implementation

1. Target model: `compiler/include/foundation/target.hpp` and `compiler/src/target.cpp` add
   `Freestanding`, a selector type with `hosted`, and one selection predicate. The local parser in
   `compiler/src/package.cpp:311-319`, the lock reader and writer, `package_resolver.cpp`,
   `main.cpp`, and `package_cli.cpp` use them. Self-hosted: `target.fn`, `driver.fn`,
   `package_model.fn`, `package_manifest.fn`, `package_lock.fn`, and `package_resolver.fn`.
2. Parser: `compiler/src/parser.cpp` selector matching and `@target` on `methods` blocks.
   Self-hosted: `parser_attribute.fn` and `parser_core.fn`. `std/prelude/uuid.fn` moves the UUID
   generators behind `@target(hosted)`.
3. Runtime split: add `runtime/src/core.c`, `runtime/src/core_print.c`,
   `runtime/include/foundation/runtime_core.h`, and `runtime/include/foundation/freestanding.h`
   with `fdn_context` and its size and alignment static assertions. Trim `runtime/src/runtime.c`
   and `runtime/include/foundation/runtime.h`, and raise the minor in
   `runtime/include/foundation/library.h`. Add `core.c` to the runtime library, the SDK install
   rules, and the compile definitions in `CMakeLists.txt`, and to both source lists in
   `compiler/src/driver.cpp`. The complete hosted suite must pass unchanged before the next step.
4. Front end: the analysis options carry the target into `sema.cpp`, `project.cpp`, and
   `application.cpp`. `sdk.cpp` owns the freestanding SDK table. Self-hosted: the matching
   `semantic_*.fn` files and `project_load.fn`.
5. C backend: the freestanding preamble and package emission in `compiler/src/codegen.cpp` and
   `compiler/selfhost/src/c_emit_program.fn`.
6. LLVM backend: triple, CPU, feature, relocation, and section options in
   `compiler/src/llvm_codegen.cpp` and `LlvmCodegenOptions`, plus CPU and feature validation for
   `FDN8006` and `FDN8007`.
7. Driver: `main.cpp` parses `--target`, `--triple`, `--cpu`, `--features`, and `--cc`.
   `driver.cpp` covers the following:
   - the freestanding `buildLibrary` path, with the lock check, Clang identification and probe
     (`FDN8008`), flags, archive writing, and bundle layout;
   - `emit-pii --triple --cpu --features`;
   - PII minor 4 fields in `package_interface.cpp` and `package_interface.hpp`;
   - the export rejection in `package_export.cpp`.
8. Tooling: the `@target` snippet and the package and lock grammars under `tools/vscode/`, and
   language-server target completion.
9. Documentation: `README.md:16-17`; `docs/language.md`: target selection, entry point, tasks, C
   ABI, and a freestanding section with the hook and context contract; `docs/compiler.md`: build
   and front end; `docs/compatibility.md`: the hook ABI under native library ABI 1;
   `docs/architecture.md`: the runtime core; `docs/stdlib/text.md`, `align.md`, `path.md`,
   `pattern.md`, and `json.md`: target availability; `CONTRIBUTING.md`: a verification row for
   `ctest --preset dev -R freestanding`.
10. Conformance tests below.

## Tests

- `packages.target-freestanding`: the manifest qualifiers `freestanding` and `hosted` parse,
  render, and select the expected dependencies, native sources, and links. `target freestanding`
  round-trips through the lock. `target hosted` in a lock reports `FDN4022`. Existing fixture
  digests are unchanged.
- `compiler.check.freestanding-selection`: a project with `@target(hosted)` and
  `@target(freestanding)` declarations and a targeted `methods` block is resolved and checked for
  the host and for `freestanding`. Only the matching declarations are active.
- `compiler.check.freestanding-reject.*`: fixtures under `tests/cases/freestanding-reject/` run
  through `assert-reject.cmake` with a new optional `TARGET` variable. The fixtures and their codes
  are:
  - `FDN2186`: task declaration, `spawn`;
  - `FDN2187`: channel construction, channel send, `select`;
  - `FDN2188`: `@blocking` import, `@callback` import;
  - `FDN2189`: `retry` policy;
  - `FDN3011`: imports of `std.fs`, `std.concurrent`, and `std.collections`;
  - `FDN3012`: `main`;
  - `FDN2005`: the `usize` literal `5000000000`;
  - `UUID.NewV4()` reports the code asserted by `tests/cases/reject/unknown-associated-function.fn`.
- `compiler.build.freestanding-symbols-llvm` and `-c`: `build-library` for `LLVM_HOST_TRIPLE`,
  then `llvm-nm` over the archive. Undefined symbols must fall within the permitted set. Package
  exports, core functions, and `fdn_context_init` must be defined. `main`, `malloc`, `free`, stdio,
  pthread, and platform symbols must be absent.
- `compiler.run.freestanding-hooks-llvm` and `-c`: a hosted C harness links the archive, defines
  the hooks over a fixed arena, and returns one statically initialized context. It calls exports
  that concatenate and return a `String`, releases it with `fdn_string_drop`, and asserts equal
  allocation and free counts. It asserts that `print("hello")` produces the write calls
  `("hello", 5)` and `("\n", 1)`. A second fixture without `print` links with no write hook.
- `compiler.run.freestanding-contexts-llvm` and `-c`:
  - Two harness threads each own a context. The context hook returns the calling thread's context
    from harness-owned thread-local storage. Both threads call exports concurrently for a fixed
    iteration count. The test asserts every result and that live allocations return to zero.
  - A Foundation export calls a native import. The import switches the harness's current context
    to a second context, calls a nested export, restores the first context, and returns. The outer
    export then completes without an `invalid frame chain` panic.
  - A nested export that panics on the second context delivers a location naming that export.
  - Nested re-entry without a context switch completes normally.
- `compiler.run.freestanding-panic-*`: an out-of-bounds index delivers `index out of bounds` with
  the expected function, file, and line. The C backend reports the Foundation function and the
  indexing statement. Optimized LLVM library code records only native boundary frames, as hosted
  libraries do, so it reports the exported C symbol at its declaration. An arena that returns
  `NULL` delivers `allocation failed`. A panic hook that returns terminates the harness
  abnormally.
- `compiler.build.freestanding-cpu`: when LLVM and the selected Clang register the targets:
  - `--triple thumbv7em-none-eabi --cpu cortex-m4`: `llvm-readelf --arch-specific` reports
    `Tag_CPU_name: cortex-m4` for every archive member;
  - `--triple riscv32-unknown-none-elf --features +zba,-c`: every member's `Tag_RISCV_arch`
    contains `zba` and lacks the `c` extension;
  - PII records `"cpu"` and the sorted `"features"` array, and the lock is byte-identical to the
    lock before the build.
- `compiler.build.freestanding-cpu-reject`: `--cpu no-such-cpu` reports `FDN8006`. The feature
  lists `+no-such-feature`, `neon`, an empty list, and a duplicate name each report `FDN8007`.
  `--cpu` without `--target freestanding` is a usage error.
- `compiler.build.freestanding-cc`: `--cc` naming a test stub reports `FDN8008` in two cases: the
  stub prints `gcc (stub) 1.0.0`, or it prints `clang version 99.0.0` and fails every compile. With
  `--cc` naming the Clang used by the WebAssembly guest tests, the build succeeds on every CI
  compiler configuration, including GCC and MSVC.
- `compiler.build.freestanding-wasm32-llvm` and `-c`: `--triple wasm32-unknown-unknown`, linked by
  `wasm-ld --no-entry` with a C file that provides the hooks and memory primitives, following
  `tests/assert-wasm-guest.cmake`. The test validates the module magic. When the WAMR provider is
  built, it also calls an export and compares its result.
- `compiler.build.freestanding-deterministic`: two builds with the same toolchain identity produce
  byte-identical archives, headers, and PII.
- `compiler.build.freestanding-cli-reject`: `--kind shared`, a missing `--triple`, `--triple`
  without `--target freestanding`, a repeated option, a host lock, and a freestanding lock without
  `--target freestanding`. `FDN8005` for a 16-bit triple is checked when LLVM registers one.
- Hosted boundary: the complete suite, `runtime.string`, `runtime.uuid`, and the panic trace tests
  pass after the split. A hosted run executes a `@target(hosted)` function.
- Self-hosted: `foundation_selfhost_verify`, the reject fixtures with identical codes, and
  `compiler.check.freestanding-selection` through the self-hosted compiler.

Build and run tests use the configured compiler when it is Clang or AppleClang. Otherwise they pass
`--cc` with the Clang used by the WebAssembly guest tests. They are registered when either is
available. The wasm32 tests follow the existing WebAssembly guest test conditions.

## Alternatives

Weak default hooks would hide a missing integration behind silent hosted fallbacks, and their
semantics differ across ELF, Mach-O, COFF, and WebAssembly. A runtime hook table registered by an
init call leaves panics before initialization undefined. Link-time symbols fail at link time
instead.

A single process-wide frame chain would forbid multi-core use and interrupt handlers that call
Foundation code. Thread-local frames require a TLS runtime that bare-metal targets lack. Making the
context hook optional would create two runtime models with different rules. A required hook keeps
one model, and a single-context integrator implements it by returning one static context.
Per-context allocation counters would report false underflows when ownership moves between
contexts.

Driver options such as `-mcpu`, `-march`, and `-mfpu` have different meanings on each
architecture. Front-end CPU and feature options use the same names that LLVM's target machine
accepts. Encoding the architecture in the target, as in `--target thumbv7em-none-eabi`, would
multiply lock keys and `@target` spellings even though source selection does not depend on the
architecture. A separate `--freestanding` flag would duplicate the target, which already drives
selection and locks.

Requiring the configured compiler to be Clang would exclude GCC and MSVC toolchains. `--cc` keeps
those toolchains usable, and Clang identification with a probe keeps the options defined. A manifest
opt-in directive cannot describe SDK packages, which have no manifests. The import check gives the
same boundary without a new directive. Shipping only `emit-c` output and `core.c` remains possible
for other toolchains, but the checked bundle with PII is the supported product. Executor hooks for
tasks would make the first freestanding ABI large and platform-shaped.
