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
_Static_assert(FOUNDATION_UI_FILE_DIALOG_SELECTED == 3,
               "ABI 1.5 dialog values must append values");

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
    if (foundation_ui_compact_icon_button(invalid, FOUNDATION_UI_ICON_BACK, &label, false, true))
        return 15;
    if (foundation_ui_begin_compact_group(invalid, &label))
        return 16;
    if (foundation_ui_begin_context_menu(invalid, 180.0f, 2))
        return 17;
    if (foundation_ui_context_menu_item(invalid, &label, true))
        return 18;
    foundation_ui_end_context_menu(invalid);
    if (foundation_ui_begin_popover(invalid, 320.0f, 240.0f))
        return 30;
    foundation_ui_end_popover(invalid);
    if (foundation_ui_create_tray(invalid, &label) != FOUNDATION_UI_INVALID)
        return 19;
    if (foundation_ui_add_tray_action(invalid, &label, &kind) != FOUNDATION_UI_INVALID)
        return 20;
    if (foundation_ui_poll_tray_action(invalid, &kind) != FOUNDATION_UI_POLL_FAILED)
        return 21;
    if (foundation_ui_request_open_file_dialog(invalid) != FOUNDATION_UI_INVALID)
        return 22;
    if (foundation_ui_poll_open_file_dialog(invalid, &kind, &text) != FOUNDATION_UI_INVALID)
        return 23;
    if (foundation_ui_slider(invalid, 5, 0, 10, 1, &width, &changed) != FOUNDATION_UI_INVALID)
        return 24;
    foundation_ui_close(&invalid);
    if (invalid != 0)
        return 25;
    return 0;
}
