#ifndef FOUNDATION_UI_H
#define FOUNDATION_UI_H

#include <foundation/runtime.h>

#include <stdbool.h>
#include <stdint.h>

#define FOUNDATION_UI_ABI_MAJOR 1
#define FOUNDATION_UI_ABI_MINOR 5
#define FOUNDATION_UI_ABI_VERSION(major, minor) ((((uint64_t)(major)) << 32U) | (uint64_t)(minor))
#define FOUNDATION_UI_ABI_CURRENT                                                                  \
    FOUNDATION_UI_ABI_VERSION(FOUNDATION_UI_ABI_MAJOR, FOUNDATION_UI_ABI_MINOR)
#define FOUNDATION_UI_ABI_MAJOR_OF(value) ((uint32_t)((uint64_t)(value) >> 32U))
#define FOUNDATION_UI_ABI_MINOR_OF(value) ((uint32_t)((uint64_t)(value) & UINT64_C(0xffffffff)))

enum foundation_ui_status {
    FOUNDATION_UI_OK = 0,
    FOUNDATION_UI_UNAVAILABLE = -1,
    FOUNDATION_UI_INVALID = -2,
    FOUNDATION_UI_FAILED = -3,
};

enum foundation_ui_frame_state {
    FOUNDATION_UI_FRAME_ACTIVE = 0,
    FOUNDATION_UI_FRAME_CLOSING = 1,
    FOUNDATION_UI_FRAME_IDLE = 2,
};

enum foundation_ui_theme {
    FOUNDATION_UI_THEME_DARK = 0,
    FOUNDATION_UI_THEME_LIGHT = 1,
};

enum foundation_ui_label_tone {
    FOUNDATION_UI_LABEL_PRIMARY = 0,
    FOUNDATION_UI_LABEL_MUTED = 1,
    FOUNDATION_UI_LABEL_ACCENT = 2,
    FOUNDATION_UI_LABEL_DANGER = 3,
};

enum foundation_ui_icon {
    FOUNDATION_UI_ICON_LINK = 0,
    FOUNDATION_UI_ICON_SHARE = 1,
    FOUNDATION_UI_ICON_TERMINAL = 2,
    FOUNDATION_UI_ICON_FOLDER = 3,
    FOUNDATION_UI_ICON_DISPLAY = 4,
    FOUNDATION_UI_ICON_THEME = 5,
    FOUNDATION_UI_ICON_HOME = 6,
    FOUNDATION_UI_ICON_ADD = 7,
    FOUNDATION_UI_ICON_WORKSPACE = 8,
    FOUNDATION_UI_ICON_KEY = 9,
    FOUNDATION_UI_ICON_SETTINGS = 10,
    FOUNDATION_UI_ICON_BACK = 11,
    FOUNDATION_UI_ICON_FORWARD = 12,
    FOUNDATION_UI_ICON_RELOAD = 13,
    FOUNDATION_UI_ICON_EXTERNAL = 14,
    FOUNDATION_UI_ICON_SPLIT = 15,
    FOUNDATION_UI_ICON_NOTIFICATION = 16,
    FOUNDATION_UI_ICON_DOWNLOAD = 17,
    FOUNDATION_UI_ICON_WEB = 18,
};

enum foundation_ui_input_kind {
    FOUNDATION_UI_INPUT_MOUSE_MOVE = 0,
    FOUNDATION_UI_INPUT_MOUSE_BUTTON = 1,
    FOUNDATION_UI_INPUT_KEY = 2,
    FOUNDATION_UI_INPUT_WHEEL = 3,
    FOUNDATION_UI_INPUT_TEXT = 4,
    FOUNDATION_UI_INPUT_MOUSE_LEAVE = 5,
};

enum foundation_ui_surface_input_mode {
    FOUNDATION_UI_SURFACE_INPUT_CAPTURED = 0,
    FOUNDATION_UI_SURFACE_INPUT_EMBEDDED = 1,
};

enum foundation_ui_poll_result {
    FOUNDATION_UI_POLL_FAILED = -1,
    FOUNDATION_UI_POLL_EMPTY = 0,
    FOUNDATION_UI_POLL_EVENT = 1,
};

enum foundation_ui_file_dialog_state {
    FOUNDATION_UI_FILE_DIALOG_IDLE = 0,
    FOUNDATION_UI_FILE_DIALOG_PENDING = 1,
    FOUNDATION_UI_FILE_DIALOG_CANCELLED = 2,
    FOUNDATION_UI_FILE_DIALOG_SELECTED = 3,
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Handles are opaque. Open, use, and close every live handle on one application UI thread.
 * Closing a window invalidates its handle, surfaces, tray, strings, buffers, and queued input. A
 * surface and tray are valid only with the window that created them.
 *
 * Status functions return FOUNDATION_UI_OK or a negative foundation_ui_status. Begin-frame returns
 * a nonnegative foundation_ui_frame_state or a negative status. Boolean widget functions return
 * false for both no activation and invalid input. Void drawing functions ignore invalid input.
 * Layout and drawing functions are valid only inside a successful begin-root/end-root pair. Row
 * heights and column ratios must be finite and positive. A successful begin-group must be paired
 * with end-group. A successful begin-context-menu must be paired with end-context-menu. Context
 * menus attach to the most recently drawn ordinary widget.
 *
 * Captured surfaces own pointer and keyboard input until release. Embedded surfaces retain keyboard
 * focus after a click but return pointer input outside their latest bounds to ordinary widgets.
 *
 * Each buffer function returns writable provider storage and its capacity. Fill width * height * 4
 * RGBA8 bytes for image buffers, or the requested byte count for terminal buffers, then call the
 * matching commit before drawing or requesting another buffer for that resource. The pointer is
 * invalidated by a resizing buffer request, surface destruction, or window close. Commit does not
 * retain caller storage.
 *
 * Secret edits permit paste but suppress clipboard copies and wipe provider buffers before release.
 * A window owns at most one open-file dialog. Request starts a single-file selection on the UI
 * thread. Poll returns PENDING until the provider callback completes, then reports CANCELLED,
 * SELECTED, or a failed status once before returning to IDLE. A selected path is an owned string.
 * Closing the window discards its pending dialog result without invalidating the callback storage.
 * String output parameters must point to initialized fdn_string values. The function drops the
 * previous value and returns an owned string that the caller must drop. Other output pointers must
 * be non-null. Open writes zero to handle on failure; close accepts zero or stale handles.
 */

uint64_t foundation_ui_provider_abi(void);
int32_t foundation_ui_open(const fdn_string* title, uint64_t width, uint64_t height,
                           uint64_t* handle);
void foundation_ui_close(uint64_t* handle);
int32_t foundation_ui_begin_frame(uint64_t handle);
int32_t foundation_ui_end_frame(uint64_t handle);
int32_t foundation_ui_set_theme(uint64_t handle, uint64_t theme);
int32_t foundation_ui_set_accent(uint64_t handle, uint64_t red, uint64_t green, uint64_t blue,
                                 uint64_t alpha);
int32_t foundation_ui_set_content_padding(uint64_t handle, uint64_t horizontal, uint64_t vertical);
int32_t foundation_ui_set_content_spacing(uint64_t handle, uint64_t horizontal, uint64_t vertical);
int32_t foundation_ui_size(uint64_t handle, uint64_t* width, uint64_t* height);
int32_t foundation_ui_set_visible(uint64_t handle, bool visible);
bool foundation_ui_visible(uint64_t handle);
int32_t foundation_ui_raise(uint64_t handle);
int32_t foundation_ui_request_open_file_dialog(uint64_t handle);
int32_t foundation_ui_poll_open_file_dialog(uint64_t handle, uint64_t* state, fdn_string* path);
int32_t foundation_ui_create_tray(uint64_t handle, const fdn_string* tooltip);
int32_t foundation_ui_set_tray_tooltip(uint64_t handle, const fdn_string* tooltip);
int32_t foundation_ui_add_tray_action(uint64_t handle, const fdn_string* label, uint64_t* action);
int32_t foundation_ui_add_tray_separator(uint64_t handle);
int32_t foundation_ui_poll_tray_action(uint64_t handle, uint64_t* action);
void foundation_ui_destroy_tray(uint64_t handle);
bool foundation_ui_begin_root(uint64_t handle);
void foundation_ui_end_root(uint64_t handle);
void foundation_ui_titlebar(uint64_t handle, const fdn_string* title, const fdn_string* subtitle);
void foundation_ui_row(uint64_t handle, float height, uint64_t columns);
void foundation_ui_row_begin(uint64_t handle, float height, uint64_t columns);
void foundation_ui_row_push(uint64_t handle, float ratio);
void foundation_ui_row_end(uint64_t handle);
bool foundation_ui_begin_group(uint64_t handle, const fdn_string* name, bool scrollable);
bool foundation_ui_begin_compact_group(uint64_t handle, const fdn_string* name);
void foundation_ui_end_group(uint64_t handle);
void foundation_ui_space(uint64_t handle, float height);
void foundation_ui_empty(uint64_t handle);
void foundation_ui_separator(uint64_t handle);
uint8_t* foundation_ui_application_icon_buffer(uint64_t handle, uint64_t width, uint64_t height,
                                               uint64_t* capacity);
int32_t foundation_ui_application_icon_commit(uint64_t handle);
void foundation_ui_application_icon(uint64_t handle);
bool foundation_ui_icon_button(uint64_t handle, uint64_t icon, const fdn_string* label,
                               bool selected, bool enabled);
bool foundation_ui_compact_icon_button(uint64_t handle, uint64_t icon, const fdn_string* label,
                                       bool selected, bool enabled);
bool foundation_ui_monogram_button(uint64_t handle, const fdn_string* monogram,
                                   const fdn_string* label, uint64_t red, uint64_t green,
                                   uint64_t blue, uint64_t alpha, bool selected, bool enabled);
bool foundation_ui_image_button(uint64_t handle, uint64_t surface, const fdn_string* label,
                                bool selected, bool enabled);
void foundation_ui_heading(uint64_t handle, const fdn_string* value);
void foundation_ui_label(uint64_t handle, const fdn_string* value, uint64_t tone, bool wrap);
uint8_t* foundation_ui_terminal_buffer(uint64_t handle, uint64_t length, uint64_t* capacity);
int32_t foundation_ui_terminal_commit(uint64_t handle, uint64_t length);
int32_t foundation_ui_terminal(uint64_t handle, float height, fdn_string* input, uint64_t* columns,
                               uint64_t* rows, bool* resized);
bool foundation_ui_button(uint64_t handle, const fdn_string* value, bool selected, bool primary);
bool foundation_ui_file_entry(uint64_t handle, const fdn_string* name, const fdn_string* details,
                              bool directory);
bool foundation_ui_begin_context_menu(uint64_t handle, float width, uint64_t items);
bool foundation_ui_context_menu_item(uint64_t handle, const fdn_string* label, bool enabled);
void foundation_ui_end_context_menu(uint64_t handle);
int32_t foundation_ui_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                           uint64_t capacity, fdn_string* result, bool* changed, bool* committed);
int32_t foundation_ui_secret_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                                  uint64_t capacity, fdn_string* result, bool* changed,
                                  bool* committed);
int32_t foundation_ui_create_surface(uint64_t handle, uint64_t* surface);
int32_t foundation_ui_destroy_surface(uint64_t handle, uint64_t surface);
uint8_t* foundation_ui_image_buffer(uint64_t handle, uint64_t surface, uint64_t width,
                                    uint64_t height, uint64_t* capacity);
int32_t foundation_ui_image_commit(uint64_t handle, uint64_t surface);
int32_t foundation_ui_image(uint64_t handle, uint64_t surface);
int32_t foundation_ui_surface_size(uint64_t handle, uint64_t surface, uint64_t* width,
                                   uint64_t* height);
int32_t foundation_ui_set_surface_input_mode(uint64_t handle, uint64_t surface, uint64_t mode);
int32_t foundation_ui_poll_surface_input(uint64_t handle, uint64_t surface, uint64_t* kind,
                                         uint64_t* x, uint64_t* y, uint64_t* button, uint64_t* key,
                                         bool* down, int64_t* delta);
int32_t foundation_ui_poll_surface_event(uint64_t handle, uint64_t surface, uint64_t* kind,
                                         uint64_t* x, uint64_t* y, uint64_t* button, uint64_t* key,
                                         bool* down, int64_t* delta, bool* control, bool* shift,
                                         bool* alt, bool* super, fdn_string* text);
bool foundation_ui_surface_captured(uint64_t handle, uint64_t surface);
bool foundation_ui_surface_focused(uint64_t handle, uint64_t surface);
int32_t foundation_ui_release_surface(uint64_t handle, uint64_t surface);

#ifdef __cplusplus
}
#endif

#endif
