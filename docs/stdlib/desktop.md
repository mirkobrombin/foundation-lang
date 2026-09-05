# `std.desktop`

`std.desktop` captures the primary desktop as RGBA pixels and applies remote input where the host
platform permits it.

```foundation
fn OpenCapture() Result<own Capturer, Error>
task Capture($capturer own Capturer) CaptureOutcome
fn OpenInput() Result<own Input, Error>
fn Input.Move(&self, x u64, y u64) Result<void, Error>
fn Input.Button(&self, button u64, down bool) Result<void, Error>
fn Input.Key(&self, key u64, down bool) Result<void, Error>
fn Input.Scroll(&self, delta i64) Result<void, Error>
```

`OpenCapture` reports the virtual desktop size on Windows and the primary display size on macOS
and X11. `Capture` returns row-major RGBA pixels with four bytes per pixel. It restores the owned
capturer beside the result so repeated capture can run in a dedicated task without shared mutable
state.

Pointer coordinates range from 0 through 32767 on each axis. Buttons and keys use Linux
input-event codes as the portable wire representation; Windows and macOS translate the supported
subset to their native key codes. Scroll values are relative wheel steps.

Linux capture requires a reachable X11 display, and input uses `/dev/uinput`. Opening the input
device returns `Permission` when the process cannot access it. Windows input uses `SendInput` and
can be rejected when the target runs at a higher integrity level. macOS input requires Accessibility
permission, while capture requires Screen Recording permission. Unsupported platforms return
`Unavailable` for both services.

`Capturer` and `Input` are owned resources. `Close` releases them early and returns false after the
first close. Dropping either value releases any remaining native handles. A frame is limited by
the runtime byte collection maximum and allocation failure remains a fatal panic.
