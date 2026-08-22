# Desktop notes

This stateful desktop application keeps its notebook and action workflow in Foundation while a
small C host presents native dialogs. The host uses Win32 on Windows, AppleScript on macOS, and
Zenity on Linux; it does not bring a GUI toolkit into the Foundation runtime.

The sample workflow adds two notes, edits and searches one, then deletes the other. Empty titles are
rejected before the native boundary, and every completed action is recorded as a notebook event.

## Run

Run the deterministic CI path on every host:

```sh
FOUNDATION_DESKTOP_HEADLESS=1 ./build/dev/foundationc run examples/desktop-notes \
  --native examples/desktop-notes/native/desktop_host.c
```

Without `FOUNDATION_DESKTOP_HEADLESS=1`, the host opens a native dialog after each step. Build on
Windows from a Developer Command Prompt; Linux needs Zenity at runtime. The host fails closed when
the current system has no supported UI.

## Build, test, debug, and distribute

`foundationc build examples/desktop-notes --native ...` emits the executable. The
`compiler.run.desktop-notes` CTest replays the same workflow headlessly and checks its seven output
lines. Use `foundationc check examples/desktop-notes` for Foundation diagnostics; a native build
failure keeps both the C compiler message and the Foundation location of the FFI call.

Distribution includes the executable, host source, `foundation.package`, and `foundation.lock`.
Linux packages must declare Zenity as a runtime dependency. macOS uses the system
`/usr/bin/osascript`, while the Windows build links User32 through its platform toolchain. Only
`foundation_desktop_is_headless` and `foundation_desktop_show` cross the native boundary.

To inspect an intentional FFI failure, omit `desktop_host.c` from a build. The linker will report
the missing `foundation_desktop_*` symbols at the integration boundary.
