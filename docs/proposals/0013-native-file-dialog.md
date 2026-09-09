# 0013: Native file selection

Status: accepted

## User problem

Native applications need a platform file picker for uploads and imports. A text field for an
absolute path does not provide normal desktop interaction and encourages applications to add their
own native boundary.

## Design

`Window.RequestOpenFileDialog()` starts one asynchronous, single-file native selection. A window
owns at most one active selection.

`Window.PollOpenFileDialog()` returns `Idle`, `Pending`, `Cancelled`, or `Selected(path)`. A terminal
result is returned once and consumed. The selected path is owned by the caller. Provider failures
use the existing `ui.Error` contract.

## Compatibility

The UI provider ABI advances from 1.4 to 1.5. Existing symbols, enum values, ownership rules, and
widget behavior remain unchanged. Existing binaries continue to link. Consumers built with SDK 1.5
require ABI minor 5.

## Diagnostics

No diagnostic code changes. A second request while one selection is active returns `Invalid`.

## Implementation

The SDL provider calls its asynchronous platform dialog on the UI thread. Callback state is
reference-counted and locked because SDL may invoke the callback on another thread. Closing a
window detaches its result while callback storage remains valid until SDL completes it.

## Tests

Native provider tests cover pending, cancellation, selection, failure, and callback storage.
C and C++ header consumers verify ABI 1.5. Relocated SDK fixtures resolve provider version 1.5.0.

## Alternatives

Product-specific C imports duplicate platform behavior and bypass the provider ownership contract.
A blocking call would stop frame processing and browser or network tasks while the dialog is open.
