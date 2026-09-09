# 0011: Image actions and content insets

Status: accepted

## User problem

Application shells need service icons supplied at runtime and edge-to-edge content beside ordinary
padded controls. `MonogramButton` cannot display a downloaded icon, while the fixed root inset
leaves unused borders around browser and remote desktop surfaces.

## Design

`Window.ImageButton` draws a window-owned RGBA surface as a compact action with provider-owned
selection, hover, activation, and tooltip behavior. The source aspect ratio is preserved inside a
28 logical pixel square.

`Window.CompactIconButton` draws the browser and window actions used by dense titlebars with
smaller provider-owned geometry.

`Window.BeginCompactGroup` applies a four logical pixel horizontal inset with no vertical inset and
keeps mouse-wheel scrolling without drawing a scrollbar.

`Window.SetContentPadding(horizontal, vertical)` sets the root inset for later frames.
`Window.SetContentSpacing(horizontal, vertical)` sets the gap between root rows and columns. Each
value is an integer logical pixel count between 0 and 256. Groups retain their own provider padding
and spacing, so an application can combine edge-to-edge surfaces with padded forms.

## Compatibility

The UI provider ABI advances from 1.2 to 1.3. Existing symbols, enum values, ownership rules, and
default root padding remain unchanged. Existing binaries continue to link. Consumers built with
SDK 1.3 require ABI minor 3.

## Diagnostics

No diagnostic code changes. A foreign or unloaded image surface makes `ImageButton` return false.
Invalid padding or spacing returns `foundation.ui.Error.Invalid`.

## Implementation

The SDL provider draws the existing surface texture without transferring ownership or enrolling it
in surface input. Content padding and spacing update the Nuklear root-window style used by later
root regions. Nested groups restore their provider spacing while active.

## Tests

Foundation provider tests cover zero content padding and spacing plus loaded image and compact icon
actions. C and C++ header consumers verify ABI 1.3, and relocated SDK fixtures resolve provider
version 1.3.0.

## Alternatives

Product-specific icon widgets would duplicate selection and input behavior. Removing the default
root inset would change every existing application. A per-window inset preserves the default while
allowing shells to own their geometry.
