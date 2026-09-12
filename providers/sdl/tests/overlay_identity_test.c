#include <foundation/ui.h>

#include <SDL3/SDL.h>

static SDL_WindowID window_id(void) {
    SDL_WindowID result = 0;
    SDL_Window** windows;
    int count = 0;
    windows = SDL_GetWindows(&count);
    if (windows != NULL && count == 1)
        result = SDL_GetWindowID(windows[0]);
    SDL_free(windows);
    return result;
}

static bool push_click(SDL_WindowID id, float y) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = id;
    event.motion.x = 100.0f;
    event.motion.y = y;
    if (!SDL_PushEvent(&event))
        return false;
    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = 100.0f;
    event.button.y = y;
    if (!SDL_PushEvent(&event))
        return false;
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = false;
    return SDL_PushEvent(&event);
}

static bool draw_picker(uint64_t handle, const fdn_string* name, bool* opened) {
    const fdn_string value = {"Selected", 8, 0};
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    *opened = foundation_ui_begin_named_picker(handle, name, &value, 120.0f);
    if (*opened)
        foundation_ui_end_picker(handle);
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

static bool draw_popover(uint64_t handle, const fdn_string* name, bool* opened) {
    const fdn_string action = {"Open", 4, 0};
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    (void)foundation_ui_action_button(handle, &action, FOUNDATION_UI_ACTION_SECONDARY, true);
    *opened = foundation_ui_begin_named_popover(handle, name, 180.0f, 100.0f);
    if (*opened)
        foundation_ui_end_popover(handle);
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

static bool draw_nested_popover(uint64_t handle, const fdn_string* name, bool close, bool* opened) {
    const fdn_string action = {"Open", 4, 0};
    const fdn_string group = {"nested-popover", 14, 0};
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    (void)foundation_ui_action_button(handle, &action, FOUNDATION_UI_ACTION_SECONDARY, true);
    *opened = foundation_ui_begin_named_popover(handle, name, 180.0f, 100.0f);
    if (*opened) {
        foundation_ui_row(handle, 52.0f, 1);
        if (!foundation_ui_begin_group(handle, &group, true))
            return false;
        if (close)
            foundation_ui_close_popover(handle);
        foundation_ui_end_group(handle);
        foundation_ui_end_popover(handle);
    }
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

int main(void) {
    const fdn_string title = {"Overlay identity", 16, 0};
    const fdn_string first = {"first-overlay", 13, 0};
    const fdn_string second = {"second-overlay", 14, 0};
    SDL_WindowID id;
    uint64_t handle = 0;
    bool opened = false;
    int result = 0;
    if (foundation_ui_open(&title, 320, 200, &handle) != FOUNDATION_UI_OK ||
        foundation_ui_set_content_padding(handle, 0, 0) != FOUNDATION_UI_OK ||
        !draw_picker(handle, &first, &opened) || opened) {
        result = 1;
    }
    id = window_id();
    if (result == 0 &&
        (!push_click(id, 20.0f) || !draw_picker(handle, &first, &opened) || !opened)) {
        result = 2;
    }
    if (result == 0 && (!draw_picker(handle, &second, &opened) || opened))
        result = 3;
    if (result == 0 &&
        (!push_click(id, 20.0f) || !draw_popover(handle, &first, &opened) || !opened)) {
        result = 4;
    }
    if (result == 0 && (!draw_popover(handle, &second, &opened) || opened))
        result = 5;
    if (result == 0 &&
        (!push_click(id, 20.0f) || !draw_nested_popover(handle, &first, true, &opened) || !opened))
        result = 6;
    if (result == 0 && (!draw_nested_popover(handle, &first, false, &opened) || opened))
        result = 7;
    foundation_ui_close(&handle);
    return result;
}
