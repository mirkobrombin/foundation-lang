# Contributing to Foundation

This file is the operational guide for changes to Foundation. A contribution should not depend on
private project notes or an unwritten build step. If a required step is missing here, update this
file in the same pull request.

## Before starting

Contributions are accepted under the repository's MIT or Apache-2.0 terms. Source, tests,
documentation, commit messages, and pull request text use English. Keep a change focused on one
behavior or one maintenance concern, and read [the compatibility contract](docs/compatibility.md)
before changing public behavior.

Use GitHub private vulnerability reporting for a security problem. The reporting boundary and
response policy are in [SECURITY.md](SECURITY.md). Do not disclose an unfixed vulnerability in a
public pull request.

## Repository map

| Path | Responsibility |
| --- | --- |
| `compiler/src/` | C++20 stage0, compiler services, language server, and command entry points |
| `compiler/include/` | Stage0 interfaces |
| `compiler/selfhost/` | Compiler implementation written in Foundation and its bootstrap tests |
| `runtime/` | Stable C11 ABI, platform adapters, and native runtime tests |
| `std/` | General-purpose Foundation packages |
| `foundation/` | Application packages built on the language and standard library |
| `tests/cases/accept/` | Programs that must compile or run |
| `tests/cases/reject/` | Programs that must fail with named diagnostics |
| `tests/projects/` | Multi-file, package, tooling, and generated-artifact fixtures |
| `tests/compatibility/` | Behavior shared with the pinned Foundation v2 baseline |
| `tools/vscode/` | VS Code client, syntax grammars, packaging, and client tests |
| `docs/` | Language, compiler, package, API, compatibility, and governance reference |
| `.github/workflows/` | Required CI and tagged release automation |

The local `website/` directory is not a repository component. Do not add it, a site server, build
output, generated compiler source, editor state, or temporary package locks to a commit. Generated
`zz*` Foundation workspace files are not used; compiler-derived declarations stay in the compiler
model and remain inspectable through their explicit emit commands.

## Development environment

The required tools are CMake 3.25 or newer, Ninja, LLVM 21.1 development files, a C11 compiler, and
a C++20 compiler. Node.js is required by the VS Code client test. Go 1.25 is required only for the
Foundation v2 compatibility fixtures. The standard build does not require OpenSSL or WAMR.

LLVM must be discoverable through its CMake package or `llvm-config-21`. When more than one LLVM is
installed, pass its CMake directory explicitly:

```sh
cmake --preset dev -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
cmake --build --preset dev
```

Run Windows commands from a Visual Studio developer shell with LLVM 21.1 and Ninja on `PATH`. The
same preset is used on every host:

```powershell
cmake --preset dev "-DLLVM_DIR=C:\path\to\llvm\lib\cmake\llvm"
cmake --build --preset dev
```

To include the Foundation v2 compatibility suite, use the pinned source revision from CI:

```sh
git clone https://github.com/mirkobrombin/go-foundation build/go-foundation-v2
git -C build/go-foundation-v2 checkout 06679f06495151fbd0d491e76121ba98b939a291
cmake --preset dev -DFOUNDATION_V2_SOURCE="$PWD/build/go-foundation-v2"
cmake --build --preset dev
```

Optional provider builds must use the OpenSSL and WAMR versions and configuration recorded in
`.github/workflows/ci.yml`. Do not change a dependency pin as part of an unrelated contribution.

## Daily work

Start from an up-to-date `main` and use a short lowercase branch name such as
`language-1-compatibility` or `plugin-abi-check`. Keep the worktree free of unrelated edits.

For Foundation source, let the compiler organize imports and format the package before review:

```sh
./build/dev/foundationc imports --write path/to/package
./build/dev/foundationc format --write path/to/package
./build/dev/foundationc lint path/to/package
```

Use `--check` instead of `--write` when verifying without changing files. Import organization may
add an import only when one dependency provides an unambiguous package and exported member match.
It never fetches a package or edits the manifest and lock.

Rebuild once the source batch is coherent, run the narrow tests for the affected subsystem, then
run the complete gate before requesting review. Do not hide a failing test, relax a diagnostic, or
increase a timeout without identifying the behavior that requires it.

## Source conventions

Foundation source follows the [Foundation Code Standard](docs/foundation-code-standard.md). Public
package declarations start with an ASCII uppercase letter; package-private declarations start with
a lowercase letter or `_`. Ownership remains visible as a plain read, `&` edit, or `$` transfer.
Comments explain ownership, ABI, grammar, safety, or a surprising lowering decision. They do not
repeat the next line.

Keep a signature on one line when it fits the active profile: 100 columns under Standard or 80
under Strict. A multiline signature ends the first line with `(`, places one parameter on each
following line, and puts `)` on its own line. Do not put the first parameter beside the opening
parenthesis in a multiline signature.

C and C++ changes follow the surrounding file. The runtime remains C11 and cannot depend on the
compiler. Stage0 remains C++20 and cannot expose a C++ type through generated code, runtime headers,
package metadata, or a public ABI. Avoid a new dependency when the existing standard library or
runtime already supplies the operation.

Documentation states current behavior in direct terms. Mark a design as specified when the current
compiler does not implement it. Examples must compile under the state label they claim.

## Requirements by change type

### Language and compiler behavior

A syntax or semantic change must update the C++ stage0 and the Foundation compiler wherever both
own that behavior. Add an accepted fixture for valid behavior and a rejected fixture for every new
failure boundary. The rejected fixture asserts the public FDN code, not diagnostic prose.

Update semantic analysis, FIR, both native backends, formatting, language services, documentation,
and package behavior only where the change reaches them. Do not add a second parser or duplicate a
type rule in editor code. Compiler-derived application declarations remain in the semantic model;
inspection commands may emit them, but a normal build cannot depend on generated workspace source.

After stage0 and self-hosted behavior agree, prove the fixed point:

```sh
cmake --build --preset dev --target foundation_selfhost_verify
```

This builds three compiler generations, requires byte-identical stage2 and stage3 output, then
checks the final compiler's version, package, LLVM, C11, build, run, and test commands.

### Runtime and public ABI

The runtime exposes capability through versioned C headers. A change to `runtime/include/` must
state ownership, lifetime, thread, and error behavior in the header or language reference. Add a C
test and, when the header is shared with C++, keep the C++ consumer test green.

Do not make a stable ABI depend on the exact compiler or SDK version. Compatibility is selected by
the ABI major and minor, target, layout, and contract. Preserve old fields and status values; add
size-tagged fields or a new minor when extension is safe, and use a new major or symbol when it is
not. Raw stage0 objects and FIR are implementation artifacts, not public ABIs.

Changes to `build-library`, generated headers, Package Interface IR, plugin descriptors, or runtime
support require a compatibility test that consumes the produced artifact rather than inspecting
only an internal structure.

### Packages, manifests, and locks

Package syntax is implemented in stage0 and the self-hosted package parser. A new directive updates
both parsers, canonical renderers, data models, unit tests, workflow fixtures, and the VS Code package
grammar. Canonical rendering is deterministic. Existing manifest and lock formats retain their
bytes unless a format revision explicitly changes them.

New projects use `language 1`. The legacy `sdk` directive remains a supported pre-release input.
One manifest cannot select both. A source compatibility change must include resolution coverage on
a future toolchain version so `language 1` cannot become an accidental SDK pin.

### Diagnostics

FDN diagnostic codes are public identifiers. Keep the same code for the same failure condition,
never reuse a retired code, and add a new code when callers must distinguish a new condition.
Messages may improve without changing tests that depend only on the code. Update every compiler and
language-server path that can report the condition.

### Tooling and documentation

Tooling consumes compiler services. Syntax grammars may color manifest or source tokens, but they do
not decide semantics. A VS Code change updates `tools/vscode/test/extension.test.js` and remains
packagable with the checked-in packaging script.

Documentation changes must agree with the implementation and its state label. Check every item in
an enumerated feature list against the grammar, public headers, or test registration. Do not expose
a feature merely because an internal type or plan mentions it.

## Verification

Use the narrowest matching command while developing:

| Surface | Command |
| --- | --- |
| Package model and resolver | `ctest --preset dev -R '^packages\.'` |
| Native plugin ABI | `ctest --preset dev -R '^runtime\.plugin$'` |
| Library and PII artifacts | `ctest --preset dev -R '^compiler\.build\.library-artifacts$'` |
| VS Code client and grammars | `ctest --preset dev -R '^tools\.vscode$'` |
| Self-hosted compiler | `cmake --build --preset dev --target foundation_selfhost_verify` |
| One named regression | `ctest --preset dev -R '<exact-test-expression>'` |

Before review, run the full local suite once:

```sh
ctest --preset dev
```

CI is the release gate. It builds with GCC, Clang, AppleClang, and MSVC; verifies the self-hosted
compiler; runs the suite three times on the primary compiler jobs; exercises optional OpenSSL and
WAMR providers; and runs parser fuzzing with sanitizers. A pull request is not ready while any
required job is pending, skipped unexpectedly, cancelled, or failing.

## Language proposals

An ordinary pull request is enough for a fix that restores documented behavior. A change to syntax,
semantics, ownership, public diagnostics, standard-library contracts, Foundation package APIs, or
a stable ABI also adds `docs/proposals/NNNN-short-name.md` using the structure in
`docs/proposals/README.md`.

The proposal identifies the user problem, exact design, source and binary compatibility,
diagnostics, implementation surfaces, conformance tests, and serious alternatives. Use short
accepted and rejected code examples when syntax is involved. Status remains `proposed` until the
maintainer accepts it. The implementation and accepted proposal must agree before a release.

## Commits and pull requests

Recent history defines the commit style. Use a short Conventional Commit subject such as
`feat: add language compatibility level`, `fix: preserve plugin ABI compatibility`, or
`docs: define release policy`. Keep the commit subject-only unless a non-obvious tradeoff cannot be
read from the diff. Do not add attribution trailers.

The repository does not require an issue before a pull request. Describe the user-visible problem,
the chosen behavior, compatibility impact, and exact verification in concise prose. Keep generated
files, local status notes, screenshots, site code, and unrelated cleanup out of the branch.

A ready pull request satisfies all of these conditions:

- the implementation and public documentation agree;
- stage0 and self-hosted paths have parity where both apply;
- accepted and rejected boundaries have tests;
- compatibility and ABI impact is explicit;
- Foundation source passes imports, format, and its selected FCS profile;
- the complete required CI is green.

## Release procedure

Only a maintainer publishes a release. Start from a clean `main` commit whose complete CI run is
green. Update the toolchain version consistently in CMake, stage0 version output, self-hosted version
output, debug producer metadata, language-server metadata, and version assertions. Search the tree
for the previous version before committing; legacy SDK fixture requirements are test data and do
not change automatically.

Run the self-hosted verification and complete local suite, push the version commit, and wait for its
required CI. Then create and push the matching tag:

```sh
git tag vMAJOR.MINOR.PATCH
git push origin vMAJOR.MINOR.PATCH
```

`.github/workflows/release.yml` verifies that the tag matches the CMake project version. It rebuilds
and tests Linux, macOS, and Windows SDKs, checks the self-hosted fixed point, installs each SDK,
uploads one archive and SHA-256 file per platform, and publishes only after every platform finishes.
If a job fails, leave the draft unpublished, fix the cause on `main`, delete the failed tag and
draft, and start again with the corrected commit.

After publication, download each archive and compare its SHA-256 file. Confirm that the installed
`foundationc`, `foundationc-selfhost`, and `foundation-ls` binaries start outside the checkout. The
release notes list compatibility changes, deprecations, security fixes, and known limitations.

The target cadence, maintainer path, and continuity model are defined in
[governance.md](docs/governance.md).
