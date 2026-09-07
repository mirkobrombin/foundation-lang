# 0007: Native UI package

Status: accepted

## User problem

Foundation applications that need native windows currently define their own C boundary, widget
model, terminal rendering, surface input, and provider packaging. Applications cannot share a UI
contract without copying that work or choosing an unrelated framework API.

## Design

`foundation.ui` defines provider-independent owning windows, frame state, layout rows and groups,
widgets, terminal rendering, and drawable surfaces. An application selects its provider by
importing a provider package. The first provider is `foundation.ui.sdl`, built from SDL3 and
Nuklear native sources.

Windows own every native resource created through them. Surfaces belong to one window and become
invalid when destroyed or when their window is dropped. Every live window must be opened, driven,
and dropped on one application UI thread. Multiple windows may remain live on that thread and the
provider routes each platform event to its target window.

The C boundary starts at ABI version 1.0. A version value stores the major in its upper 32 bits and
the minor in its lower 32 bits. Clients accept providers with the required major and at least the
required minor. Handles are opaque identifiers and never expose native pointers. Frame states are
nonnegative and failures use negative status values, so every result is unambiguous.

## Compatibility

The package is additive to Language 1 and does not change existing source or runtime behavior. The
base runtime remains independent of SDL3. Importing `foundation.ui.sdl` adds the provider's native
sources and SDL3 link requirement.

ABI 1 preserves existing functions, status values, and layouts. Compatible additions increment
the minor version. An incompatible signature, lifetime rule, status meaning, or layout requires a
new major or a new symbol.

## Diagnostics

No diagnostic code changes. Provider failures use the `foundation.ui.Error` values `Unavailable`,
`Invalid`, and `Failed`.

## Implementation

The SDK installs the `foundation.ui` sources, the SDL provider package, and the public
`foundation/ui.h` header. The provider owns window and renderer state, fonts, edit buffers,
terminal state, textures, event queues, and input capture. CMake keeps SDL support optional and CI
builds a pinned SDL revision on Linux, macOS, and Windows.

## Tests

Foundation package tests exercise window creation, multiple live windows, surface ownership,
surface destruction, widgets, nested groups, and terminal rendering through LLVM and C11. C and
C++ consumers compile the public header, check ABI version encoding, and reject invalid handles.
Provider builds treat native warnings as errors on every supported host.

## Alternatives

Keeping UI code inside each application avoids a shared package but repeats the same native
boundary and makes compatible fixes application-specific. Linking SDL3 into the base runtime makes
windowing implicit for command-line and server programs. A web-only interface does not cover
native terminal, desktop, and low-latency surface use cases.
