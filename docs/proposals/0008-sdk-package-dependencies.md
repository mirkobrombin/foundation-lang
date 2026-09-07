# 0008: SDK package dependencies

Status: accepted

## User problem

Optional first-party providers ship inside the Foundation SDK, but an external project cannot name
them without a checkout-relative path or a registry release. Either choice makes installed SDK
content depend on unrelated project layout or package transport.

## Design

The package source kind `sdk` resolves a package at a relative path under the configured SDK root.
For example:

```text
dependency foundation.ui.sdl 1.0.0 sdk providers/sdl
```

The lock retains the package version, source kind, SDK-relative location, and source digest. The
loader reads the exact installed path and rejects content that no longer matches the lock. SDK
locations cannot be absolute, contain parent traversal, or resolve outside the SDK root.

SDK packages participate in version solving, target selection, runtime and test scopes, native
source propagation, and dependency-cycle checks. They never enter the registry cache or registry
transport reports.

## Compatibility

The new source kind is additive to `foundation.package/v1` and `foundation.lock/v1`. Existing path
and registry entries keep their bytes and behavior.

## Diagnostics

Malformed SDK locations report `FDN4009` in manifests or `FDN4023` in locks. A missing SDK package
causes the existing `FDN4052` resolution conflict. Missing, changed, or mismatched locked content
reports `FDN4112` before compilation.

## Implementation

The bootstrap and self-hosted compilers resolve SDK entries through the same configured SDK root
used for `std` and `foundation` packages. Package verification reads SDK content directly and checks
its digest. Fetch, prune, requirements, and locked-release commands continue to operate only on
registry packages.

## Tests

Manifest and lock tests cover canonical SDK entries. Resolver and locked-project tests consume the
installed SDL provider. Optional-provider CI resolves, verifies, and builds an external UI consumer
with both LLVM and C11 backends.

## Alternatives

Publishing first-party providers to the registry would make an SDK feature depend on network
transport and duplicate the shipped source. Treating providers as project paths would expose the
Foundation checkout layout and fail for installed toolchains.
