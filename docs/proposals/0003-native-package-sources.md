# 0003: Native package sources

Status: accepted

## User problem

A package that carries a small C adapter currently requires every application to repeat its source
file and link libraries on the command line. That information is lost when the package is published
and consumed from a registry.

## Design

`native_source <path> [target <platform>]` declares one C source file compiled with the package and
with applications that consume it. The path must sit inside a `foreign c ... path` tree. The package
snapshot includes that tree so local and registry builds use the same checked bytes.

```text
native_source native/fuse.c target linux
native_link fuse target linux
foreign c libfuse 2.9.9 path native abi c/v1
```

The compiler rejects non-C paths, overlapping target declarations for the same file, sources
outside their declared foreign tree, missing files, and symbolic links. Native sources and links
are collected from the locked runtime dependency graph. Explicit `--native` and `--native-link`
arguments remain available for application-selected providers.

## Compatibility

The directive is additive. Existing Language 1 manifests, locks, source files, native libraries,
and plugins retain their behavior and package digest. Adding `native_source` changes that package's
manifest and digest because the declared foreign tree becomes part of its immutable snapshot.

## Diagnostics

`FDN4015` reports malformed, duplicate, or unowned native source declarations. Existing package
source diagnostics report missing files, unsafe paths, symbolic links, collisions, and size limits.

## Implementation

The stage0 and self-hosted package parsers store and render native sources in canonical order. The
package snapshot includes only foreign path trees that own a declared native source. Build, run,
test, and library commands collect active sources and links from the resolved graph. The VS Code
package grammar recognizes the directive.

## Tests

Parser tests cover accepted, targeted, duplicate, non-C, and unowned declarations. Package tests
prove that foreign bytes affect the digest only when a native source uses that tree. Snapshot tests
copy the foreign tree, and a consumer runs a transitive native dependency on both backends without
command-line native inputs. Self-hosted LLVM and C tests cover the same manifest behavior.

## Alternatives

Keeping native inputs only on the command line prevents registry packages from being complete.
Storing arbitrary compiler flags in the manifest would expose toolchain-specific behavior and
weaken deterministic builds. A source directive plus named libraries keeps the boundary portable
and inspectable.
