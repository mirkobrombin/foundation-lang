# Desktop notes

This is a stateful Foundation desktop application with one real native host boundary. The package
keeps a collection of notes, rejects an empty title, records notebook events, adds two notes, edits
one, searches it, and deletes the other. Each completed action is presented through the same
Foundation workflow. The C host is limited to platform UI: Win32 on Windows, AppleScript on macOS,
and Zenity on Linux. No GUI toolkit is part of the Foundation runtime or required by CI.

## Run

Run the deterministic CI path on every host:

```sh
FOUNDATION_DESKTOP_HEADLESS=1 ./build/dev/foundationc run examples/desktop-notes \
  --native examples/desktop-notes/native/desktop_host.c
```

On Windows build from a Developer Command Prompt. Without `FOUNDATION_DESKTOP_HEADLESS=1`, the host
opens a native dialog after each add, edit, search, delete, and final notebook action. Linux
requires Zenity at runtime. The host fails closed when no supported desktop UI is available.

## Build, test, debug, and distribute

`foundationc build examples/desktop-notes --native ...` emits the host executable. The registered
`stage0.run.desktop-notes` CTest uses the same native input, replays the workflow headlessly, and
checks its seven output lines. Debug Foundation diagnostics with `foundationc check
examples/desktop-notes`; native failures retain the C compiler diagnostic and the source location
of the FFI call.

Distribute the executable with its host source, `foundation.package`, and `foundation.lock`. Linux
packages must state Zenity as a runtime dependency. macOS distribution uses the system
`/usr/bin/osascript`; Windows links User32 through the platform toolchain. The native boundary is
two imports only: `foundation_desktop_is_headless` and `foundation_desktop_show`.

To inspect an intentional FFI failure, omit `desktop_host.c` from a build. The native linker reports
the missing `foundation_desktop_*` symbols. Keep the host shim separate from Foundation business
logic so that diagnostic identifies the integration boundary directly.
