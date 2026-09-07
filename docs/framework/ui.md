# `foundation.ui`

`foundation.ui` defines the first-party interface for native application windows. Applications
choose a provider package explicitly; the base runtime does not link a window system or rendering
library.

The SDK includes `foundation.ui.sdl`, an optional SDL3 and Nuklear provider. Importing that package
adds its native sources and SDL3 link requirement through `foundation.package`.

```text
dependency foundation.ui.sdl 1.0.0 sdk providers/sdl
```

```foundation
package example

import foundation.ui
import foundation.ui.sdl

fn main() i32 {
    const opened = sdl.Open("Example", 960, 640) else { return 1 }
    var window = opened
    window.SetTheme(.Dark) else { return 1 }
    window.SetAccent(ui.Color {
        Red = 95
        Green = 221
        Blue = 122
        Alpha = 255
    }) else { return 1 }

    while true {
        const frame = window.BeginFrame() else { return 1 }
        match frame {
            Closing: { return 0 }
            Active: {}
            Idle: {}
        }
        if window.BeginRoot() {
            window.Titlebar("Example", "ready")
            window.Row(36.0, 1)
            window.Label("Hello, Foundation", .Primary, false)
            window.EndRoot()
        }
        window.EndFrame() else { return 1 }
    }
    0
}
```

## Windows and frames

`Window` owns the native window, renderer, fonts, terminal state, edit buffers, textures, and input
queues. Dropping it releases those resources. `BeginFrame` returns `Active`, `Idle`, or `Closing`;
an idle result permits the application to avoid work until its own tasks or the platform produce
another event.

All live windows are opened, driven, and dropped on one application UI thread. The SDL provider
routes events between windows on that thread; concurrent window calls are unsupported.

The drawing API uses immediate-mode rows and groups. Widget calls apply to the current row or group
between `BeginRoot` and `EndRoot`. The provider supplies client-side window controls and removes
rounded corners while maximized on platforms that support shaped windows.

## Surfaces

A surface carries RGBA8 pixels from a browser, remote desktop, game renderer, or another producer.
Each window can create up to 16 surfaces. Their textures, bounds, draw order, input queues, and
capture state remain independent.

```foundation
const surface = window.CreateSurface() else { return 1 }
window.UpdateSurface(surface, width, height, pixels) else { return 1 }
window.DrawSurface(surface) else { return 1 }
```

`UpdateSurface` consumes the pixel buffer. `DrawSurface` preserves the source aspect ratio inside
the active cell. `DestroySurface` releases the provider resource; using its identifier afterward
returns `Invalid`.

`PollSurfaceInput` reports coordinates in the inclusive 0 through 32767 range. Buttons and keys
use the same Linux input-event wire representation as `std.desktop`, so remote input can cross
platform boundaries without rewriting the protocol. Pressing F8 releases a captured surface and
emits release events for held keys and buttons. If the bounded input queue fills, the next poll
returns `Failed` after releasing capture and every held input.

## Terminal

The terminal widget accepts an owned byte stream, interprets UTF-8 and the supported ANSI terminal
sequences, and returns input plus its current row and column count. Input is collected by the
provider clipboard and text-input integration. The application remains responsible for the PTY or
remote transport and for forwarding resize events.

## Provider boundary

The C header `foundation/ui.h` defines UI ABI 1.0. The provider reports the major in the upper 32
bits and the minor in the lower 32 bits. Clients accept the required major and a minor at least as
new as the contract they use. Existing functions and layouts remain stable under the Foundation
compatibility contract; compatible releases may append functions or provider capabilities. The
current provider is built only when `FOUNDATION_BUILD_SDL_PROVIDER=ON` or when an application
imports `foundation.ui.sdl`.
