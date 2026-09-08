# 0009: Surface input and layout feedback

Status: accepted

## User problem

An embedded browser can draw inside a `foundation.ui` surface but cannot discover the logical cell
assigned to it. It must guess a viewport size, which introduces scaling or unused space after a
window resize. Captured surfaces also receive physical key transitions without committed text or
modifier state, so text fields, shortcuts, and input methods do not work through the surface.
Exclusive capture is correct for a remote desktop but prevents an embedded browser from returning
pointer input to its surrounding navigation controls.

Application rails need common navigation symbols. Restricting `IconButton` to the first six
symbols forces text placeholders or application-owned widget code for ordinary native controls.

## Design

`Window.SurfaceSize` returns the logical cell from the latest successful `DrawSurface` call. It
fails before the first draw and when the surface does not belong to the window.

`SurfaceInput` adds Control, Shift, Alt, Super, and Text fields. `Text` events carry committed UTF-8
text. Other events carry an empty string. Physical keys and buttons retain their existing Linux
input-event values. The C provider adds `foundation_ui_poll_surface_event`; the existing
`foundation_ui_poll_surface_input` symbol remains available with ABI 1.0 behavior and skips text
events it cannot represent.

`SetSurfaceInputMode` selects `Captured` or `Embedded`. Captured remains the default and retains the
ABI 1.0 F8 release behavior. An embedded surface keeps keyboard focus after activation, forwards
pointer events within its bounds, and yields a click outside its bounds to the window UI.
`SurfaceFocused` reports keyboard ownership in either mode; `SurfaceCaptured` preserves its ABI 1.0
exclusive-capture meaning. `MouseLeave` clears hover state in embedded consumers, and repeated
physical key-down events remain in the input stream.

`Icon` appends common home, workspace, browser navigation, notification, download, and web symbols.
Existing icon values remain unchanged.

`MonogramButton` adds a provider-drawn fallback for dynamic services and accounts. The application
supplies the short label and color; product-specific symbols do not enter the provider ABI.

## Compatibility

The provider ABI advances from 1.0 to 1.1. Every ABI 1.0 symbol, value, signature, and lifetime rule
remains unchanged. New enum values are appended. Existing binaries continue to link against the
new provider; consumers of the new functions require ABI minor 1.

## Diagnostics

No diagnostic code changes. An undrawn surface reports `foundation.ui.Error.Failed`. Invalid or
foreign surface identifiers report `Invalid`.

## Implementation

The SDL provider retains both the full layout cell and the aspect-fitted image bounds. Pointer
mapping continues to use the image bounds. Text input and modifier state enter the same bounded
per-surface queue as key, pointer, and wheel events. Legacy polling drops text events rather than
returning a kind unknown to ABI 1.0 callers.

## Tests

Provider tests cover the undrawn failure, the reported cell after drawing, surface mode selection,
every Foundation enum mapping, and C ABI value stability. C and C++ header consumers compile
against ABI 1.1. Browser consumers verify viewport resize, focus transfer, text entry, shortcuts,
and access to surrounding controls through their embedded engine.

## Alternatives

Computing viewport sizes in each application duplicates provider padding and scaling rules.
Sending printable key codes cannot represent input methods or composed Unicode text. Replacing the
old poll symbol would break ABI 1.0 binaries; the additive symbol keeps them valid.
