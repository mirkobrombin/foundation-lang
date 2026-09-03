# Compatibility

Foundation versions the language, toolchain, packages, and native boundaries separately. A project
chooses the language rules it wants; installing a newer compiler does not opt that project into a
new language.

## Language 1

New projects declare their language level in `foundation.package`:

```text
format foundation.package/v1
name example.app
version 1.0.0
language 1
source src
```

`language 1` is the permanent compatibility line for the first stable language. A valid Language 1
program continues to compile on future Foundation toolchains with the same specified behavior.
Future language levels are explicit manifest changes. Every future toolchain that introduces a new
level must continue to accept Language 1.

This promise covers the grammar, type and ownership rules, observable runtime behavior, public
standard-library APIs, public Foundation package APIs, and documented diagnostic codes. A public
API may be deprecated, but it remains available to Language 1 source.

The promise does not cover behavior that the specification leaves undefined, invalid programs,
diagnostic wording, compiler bugs, panic text, source formatting, or advisory FCS findings. A fix
for a soundness or security defect may reject code that relied on the defect. Such a change needs a
release note, a stable diagnostic, and a migration path when one can be provided.

The pre-release `sdk <requirement>` manifest directive remains accepted for existing projects. It
selects a toolchain range and retains its original meaning. A manifest selects either `language 1`
or `sdk`, never both. `foundationc package init` writes `language 1`.

## Toolchain versions

Compiler and SDK releases use `MAJOR.MINOR.PATCH`. Toolchain versions describe delivery and fixes;
they do not replace the language level.

- A patch release fixes defects without adding a language feature.
- A minor release may add compatible tools, APIs, and Language 1 syntax that cannot change the
  meaning of an existing valid program.
- A major release may change toolchain operation, but it still accepts Language 1 and the stable
  ABI generations listed here.

Package versions use semantic version requirements independently of both language and toolchain
versions.

## Native library ABI 1

`foundationc build-library` publishes the stable C ABI boundary. The installed
`foundation/library.h` declares `FOUNDATION_LIBRARY_ABI_MAJOR` and
`FOUNDATION_LIBRARY_ABI_MINOR`. Package Interface IR records the ABI generation, language level,
target, public layouts, ownership modes, exports, imports, and transitive native links.

A shared or static library built by a stable Foundation toolchain for ABI major 1 can be linked by
a later Foundation toolchain on the same target and platform C ABI. The later toolchain keeps the
ABI 1 runtime entry points and data contracts. Compiler implementation details, Foundation IR, C++
types, and raw compiler-produced object files are not public binary interfaces.

Library authors still own their exported ABI. Removing or changing an exported symbol, changing a
checked public layout, or changing ownership at the boundary is an incompatible package change.
`native_soversion` records that package decision. Adding an export is compatible. PII minor
versions may add metadata fields without changing ABI major 1.

If Foundation needs an incompatible native boundary, it receives a new ABI major and new symbols.
ABI major 1 remains available to future toolchains so an existing precompiled library does not
need rebuilding merely because the compiler changed.

## Plugin ABI 1

Native plugins negotiate `FDN_PLUGIN_ABI_MAJOR` and `FDN_PLUGIN_ABI_MINOR`. The host accepts ABI
major 1 descriptors whose minor level does not exceed the host level, then checks the target,
lifecycle contract, descriptor size, text, and callbacks before creating plugin state.

The SDK version in the host and descriptor identifies the producing toolchain for diagnostics. It
does not decide compatibility. A plugin built with an older Foundation toolchain continues to load
in a later host when its ABI, target, and contract match. A new incompatible contract uses a new
query symbol or ABI major instead of reinterpreting an existing descriptor.

WebAssembly plugins have their own `foundation:plugin` ABI version. Process plugins use their
documented JSONL protocol. Each boundary advances independently and keeps its previous stable major
available.

## Changes to this contract

A change to language behavior, public runtime ABI, plugin ABI, or this compatibility promise needs
a language proposal and conformance tests. The proposal must identify the oldest source and binary
artifacts that remain valid. The process is defined in [CONTRIBUTING.md](../CONTRIBUTING.md).
