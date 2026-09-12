#include <foundation/ui.h>

#include <stdint.h>

_Static_assert(FOUNDATION_UI_UNAVAILABLE < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_INVALID < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_FAILED < 0, "provider errors must be negative");
_Static_assert(FOUNDATION_UI_FRAME_ACTIVE >= 0, "frame states must be nonnegative");
_Static_assert(FOUNDATION_UI_FRAME_CLOSING >= 0, "frame states must be nonnegative");
_Static_assert(FOUNDATION_UI_FRAME_IDLE >= 0, "frame states must be nonnegative");
_Static_assert(FOUNDATION_UI_ICON_LINK == 0, "ABI 1.0 icon values must remain stable");
_Static_assert(FOUNDATION_UI_ICON_THEME == 5, "ABI 1.0 icon values must remain stable");
_Static_assert(FOUNDATION_UI_ICON_HOME == 6, "ABI 1.1 icons must append values");
_Static_assert(FOUNDATION_UI_INPUT_WHEEL == 3, "ABI 1.0 input values must remain stable");
_Static_assert(FOUNDATION_UI_INPUT_TEXT == 4, "ABI 1.1 input must append values");
_Static_assert(FOUNDATION_UI_INPUT_MOUSE_LEAVE == 5, "ABI 1.1 input must append values");
_Static_assert(FOUNDATION_UI_SURFACE_INPUT_CAPTURED == 0,
               "ABI 1.1 captured input must preserve the default");
_Static_assert(FOUNDATION_UI_SURFACE_INPUT_EMBEDDED == 1,
               "ABI 1.1 embedded input must append a value");
_Static_assert(FOUNDATION_UI_FILE_DIALOG_IDLE == 0, "ABI 1.5 dialog values must remain stable");
_Static_assert(FOUNDATION_UI_FILE_DIALOG_SELECTED == 3, "ABI 1.5 dialog values must append values");
_Static_assert(FOUNDATION_UI_SEGMENT_SINGLE == 0, "ABI 1.8 segment values must remain stable");
_Static_assert(FOUNDATION_UI_SEGMENT_LAST == 3, "ABI 1.8 segment values must append values");
_Static_assert(FOUNDATION_UI_ACTION_SECONDARY == 0, "ABI 1.9 action values must remain stable");
_Static_assert(FOUNDATION_UI_ACTION_DESTRUCTIVE == 2, "ABI 1.9 action values must append values");

int main(void) {
    uint64_t invalid = UINT64_MAX;
    uint64_t kind = 0;
    uint64_t x = 0;
    uint64_t y = 0;
    uint64_t button = 0;
    uint64_t key = 0;
    uint64_t width = 0;
    uint64_t height = 0;
    int64_t delta = 0;
    bool down = false;
    bool control = false;
    bool shift = false;
    bool alt = false;
    bool super = false;
    bool changed = false;
    fdn_string text = {0};
    const fdn_string label = {"Action", 6, 0};
    if (foundation_ui_provider_abi() != FOUNDATION_UI_ABI_CURRENT)
        return 1;
    if (FOUNDATION_UI_ABI_MAJOR_OF(foundation_ui_provider_abi()) != FOUNDATION_UI_ABI_MAJOR)
        return 2;
    if (FOUNDATION_UI_ABI_MINOR_OF(foundation_ui_provider_abi()) != FOUNDATION_UI_ABI_MINOR)
        return 3;
    if (foundation_ui_begin_frame(invalid) != FOUNDATION_UI_INVALID)
        return 4;
    if (foundation_ui_wait(invalid, 1) != FOUNDATION_UI_INVALID)
        return 4;
    if (foundation_ui_surface_size(invalid, invalid, &width, &height) != FOUNDATION_UI_INVALID)
        return 5;
    if (foundation_ui_poll_surface_event(invalid, invalid, &kind, &x, &y, &button, &key, &down,
                                         &delta, &control, &shift, &alt, &super,
                                         &text) != FOUNDATION_UI_POLL_FAILED) {
        return 6;
    }
    if (foundation_ui_set_surface_input_mode(
            invalid, invalid, FOUNDATION_UI_SURFACE_INPUT_EMBEDDED) != FOUNDATION_UI_INVALID) {
        return 7;
    }
    if (foundation_ui_surface_focused(invalid, invalid))
        return 8;
    if (foundation_ui_set_visible(invalid, true) != FOUNDATION_UI_INVALID)
        return 9;
    if (foundation_ui_visible(invalid))
        return 10;
    if (foundation_ui_raise(invalid) != FOUNDATION_UI_INVALID)
        return 11;
    if (foundation_ui_set_content_padding(invalid, 0, 0) != FOUNDATION_UI_INVALID)
        return 12;
    if (foundation_ui_set_content_spacing(invalid, 0, 0) != FOUNDATION_UI_INVALID)
        return 13;
    if (foundation_ui_image_button(invalid, invalid, &label, false, true))
        return 14;
    if (foundation_ui_image_fill(invalid, invalid) != FOUNDATION_UI_INVALID)
        return 52;
    if (foundation_ui_compact_icon_button(invalid, FOUNDATION_UI_ICON_BACK, &label, false, true))
        return 15;
    if (foundation_ui_begin_compact_group(invalid, &label))
        return 16;
    if (foundation_ui_begin_surface_group(invalid, &label))
        return 50;
    if (foundation_ui_begin_context_menu(invalid, 180.0f, 2))
        return 17;
    if (foundation_ui_context_menu_item(invalid, &label, true))
        return 18;
    foundation_ui_end_context_menu(invalid);
    if (foundation_ui_begin_popover(invalid, 320.0f, 240.0f))
        return 30;
    foundation_ui_end_popover(invalid);
    if (foundation_ui_switch(invalid, false, true))
        return 31;
    if (foundation_ui_toggle_item(invalid, &label, &label, false, true))
        return 41;
    if (foundation_ui_check_item(invalid, &label, &label, false, true))
        return 46;
    if (foundation_ui_radio_item(invalid, &label, &label, false, true))
        return 47;
    foundation_ui_notice(invalid, &label, &label, FOUNDATION_UI_LABEL_PRIMARY);
    if (foundation_ui_action_item(invalid, &label, &label, &label, FOUNDATION_UI_ACTION_SECONDARY,
                                  true))
        return 42;
    if (foundation_ui_choice_item(invalid, &label, &label, &label, true))
        return 43;
    foundation_ui_property_item(invalid, &label, &label);
    foundation_ui_empty_state(invalid, FOUNDATION_UI_ICON_DOWNLOAD, &label, &label);
    if (foundation_ui_segment(invalid, &label, false, FOUNDATION_UI_SEGMENT_SINGLE, true))
        return 32;
    if (foundation_ui_action_button(invalid, &label, FOUNDATION_UI_ACTION_PRIMARY, true))
        return 33;
    if (foundation_ui_navigation_item(invalid, &label, false, true))
        return 34;
    if (foundation_ui_sidebar_item(invalid, FOUNDATION_UI_ICON_SHIELD, &label, false, true))
        return 48;
    if (foundation_ui_list_item(invalid, &label, &label, false, true))
        return 35;
    if (foundation_ui_begin_picker(invalid, &label, 120.0f))
        return 36;
    if (foundation_ui_begin_named_picker(invalid, &label, &label, 120.0f))
        return 44;
    if (foundation_ui_picker_item(invalid, &label, false, true))
        return 37;
    foundation_ui_end_picker(invalid);
    if (foundation_ui_begin_named_popover(invalid, &label, 320.0f, 240.0f))
        return 45;
    if (foundation_ui_begin_toolbar_popover(invalid, &label, FOUNDATION_UI_ICON_NOTIFICATION,
                                            &label, 320.0f, 240.0f, true))
        return 49;
    foundation_ui_close_popover(invalid);
    if (foundation_ui_progress(invalid, 1, 2) != FOUNDATION_UI_INVALID)
        return 38;
    if (foundation_ui_edit_hint(invalid, &label, &label, &label, 32, &text, &changed, &down) !=
        FOUNDATION_UI_INVALID) {
        return 40;
    }
    if (foundation_ui_action_edit(invalid, &label, &label, &label, &label,
                                  FOUNDATION_UI_ACTION_SECONDARY, 32, true, &text, &changed, &down,
                                  &control) != FOUNDATION_UI_INVALID) {
        return 51;
    }
    if (foundation_ui_create_tray(invalid, &label) != FOUNDATION_UI_INVALID)
        return 19;
    if (foundation_ui_add_tray_action(invalid, &label, &kind) != FOUNDATION_UI_INVALID)
        return 20;
    if (foundation_ui_poll_tray_action(invalid, &kind) != FOUNDATION_UI_POLL_FAILED)
        return 21;
    if (foundation_ui_request_open_file_dialog(invalid) != FOUNDATION_UI_INVALID)
        return 22;
    if (foundation_ui_request_open_folder_dialog(invalid) != FOUNDATION_UI_INVALID)
        return 39;
    if (foundation_ui_poll_open_file_dialog(invalid, &kind, &text) != FOUNDATION_UI_INVALID)
        return 23;
    if (foundation_ui_slider(invalid, 5, 0, 10, 1, &width, &changed) != FOUNDATION_UI_INVALID)
        return 24;
    foundation_ui_close(&invalid);
    if (invalid != 0)
        return 25;
    return 0;
}
