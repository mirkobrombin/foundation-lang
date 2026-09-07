#ifndef FOUNDATION_UI_H
#define FOUNDATION_UI_H

#include <foundation/runtime.h>

#include <stdbool.h>
#include <stdint.h>

#define FOUNDATION_UI_ABI_MAJOR 1
#define FOUNDATION_UI_ABI_MINOR 0
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
};

enum foundation_ui_input_kind {
    FOUNDATION_UI_INPUT_MOUSE_MOVE = 0,
    FOUNDATION_UI_INPUT_MOUSE_BUTTON = 1,
    FOUNDATION_UI_INPUT_KEY = 2,
    FOUNDATION_UI_INPUT_WHEEL = 3,
};

enum foundation_ui_poll_result {
    FOUNDATION_UI_POLL_FAILED = -1,
    FOUNDATION_UI_POLL_EMPTY = 0,
    FOUNDATION_UI_POLL_EVENT = 1,
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Handles are opaque. Open, use, and close every live handle on one application UI thread.
 * Closing a window invalidates its handle, surfaces, strings, buffers, and queued input. A surface
 * is valid only with the window that created it and until it is destroyed.
 *
 * Status functions return FOUNDATION_UI_OK or a negative foundation_ui_status. Begin-frame returns
 * a nonnegative foundation_ui_frame_state or a negative status. Boolean widget functions return
 * false for both no activation and invalid input. Void drawing functions ignore invalid input.
 * Layout and drawing functions are valid only inside a successful begin-root/end-root pair. Row
 * heights and column ratios must be finite and positive. A successful begin-group must be paired
 * with end-group.
 *
 * Each buffer function returns writable provider storage and its capacity. Fill width * height * 4
 * RGBA8 bytes for image buffers, or the requested byte count for terminal buffers, then call the
 * matching commit before drawing or requesting another buffer for that resource. The pointer is
 * invalidated by a resizing buffer request, surface destruction, or window close. Commit does not
 * retain caller storage.
 *
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
int32_t foundation_ui_size(uint64_t handle, uint64_t* width, uint64_t* height);
bool foundation_ui_begin_root(uint64_t handle);
void foundation_ui_end_root(uint64_t handle);
void foundation_ui_titlebar(uint64_t handle, const fdn_string* title, const fdn_string* subtitle);
void foundation_ui_row(uint64_t handle, float height, uint64_t columns);
void foundation_ui_row_begin(uint64_t handle, float height, uint64_t columns);
void foundation_ui_row_push(uint64_t handle, float ratio);
void foundation_ui_row_end(uint64_t handle);
bool foundation_ui_begin_group(uint64_t handle, const fdn_string* name, bool scrollable);
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
void foundation_ui_heading(uint64_t handle, const fdn_string* value);
void foundation_ui_label(uint64_t handle, const fdn_string* value, uint64_t tone, bool wrap);
uint8_t* foundation_ui_terminal_buffer(uint64_t handle, uint64_t length, uint64_t* capacity);
int32_t foundation_ui_terminal_commit(uint64_t handle, uint64_t length);
int32_t foundation_ui_terminal(uint64_t handle, float height, fdn_string* input, uint64_t* columns,
                               uint64_t* rows, bool* resized);
bool foundation_ui_button(uint64_t handle, const fdn_string* value, bool selected, bool primary);
bool foundation_ui_file_entry(uint64_t handle, const fdn_string* name, const fdn_string* details,
                              bool directory);
int32_t foundation_ui_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                           uint64_t capacity, fdn_string* result, bool* changed, bool* committed);
int32_t foundation_ui_create_surface(uint64_t handle, uint64_t* surface);
int32_t foundation_ui_destroy_surface(uint64_t handle, uint64_t surface);
uint8_t* foundation_ui_image_buffer(uint64_t handle, uint64_t surface, uint64_t width,
                                    uint64_t height, uint64_t* capacity);
int32_t foundation_ui_image_commit(uint64_t handle, uint64_t surface);
int32_t foundation_ui_image(uint64_t handle, uint64_t surface);
int32_t foundation_ui_poll_surface_input(uint64_t handle, uint64_t surface, uint64_t* kind,
                                         uint64_t* x, uint64_t* y, uint64_t* button, uint64_t* key,
                                         bool* down, int64_t* delta);
bool foundation_ui_surface_captured(uint64_t handle, uint64_t surface);
int32_t foundation_ui_release_surface(uint64_t handle, uint64_t surface);

#ifdef __cplusplus
}
#endif

#endif
