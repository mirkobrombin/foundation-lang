# 0012: Context menus

Status: accepted

## User problem

Dense native applications need secondary actions without placing permanent buttons beside each
item. Text-edit fields have provider-owned menus, but applications cannot attach the same
interaction to chat messages, files, or other actions.

## Design

`Window.BeginContextMenu(width, items)` opens a context menu for the preceding ordinary widget.
The provider owns row height and derives popup height from the declared item count. A successful
begin must be paired with `Window.EndContextMenu` during the same frame.

`Window.ContextMenuItem(label, enabled)` draws one action and reports activation. Disabled actions
remain visible but cannot close the menu or report activation.

## Compatibility

The UI provider ABI advances from 1.3 to 1.4. Existing symbols, enum values, ownership rules, and
widget behavior remain unchanged. Existing binaries continue to link. Consumers built with SDK 1.4
require ABI minor 4.

## Diagnostics

No diagnostic code changes. Invalid handles, dimensions, item counts, strings, or menu ordering
return false or make `EndContextMenu` a no-op under the existing widget error contract.

## Implementation

The SDL provider records the latest ordinary widget bounds and uses Nuklear's nonblocking
contextual popup. Labels retain their Foundation string length and are not treated as terminated C
strings.

## Tests

Foundation provider tests exercise enabled and disabled items. C and C++ header consumers verify
ABI 1.4, and relocated SDK fixtures resolve provider version 1.4.0.

## Alternatives

Product-specific action buttons consume permanent space and duplicate menu behavior. Exposing raw
pointer coordinates would make application layout depend on provider internals.
