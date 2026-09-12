#include <foundation/ui.h>

#include <SDL3/SDL.h>

#include <string.h>

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

static bool draw_fields(uint64_t handle, fdn_string* first, fdn_string* second) {
    const fdn_string first_name = {"first", 5, 0};
    const fdn_string second_name = {"second", 6, 0};
    fdn_string edited = {0};
    bool changed = false;
    bool committed = false;
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_edit(handle, &first_name, first, 32, &edited, &changed, &committed) !=
        FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(first);
    *first = edited;
    edited = (fdn_string){0};
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_edit(handle, &second_name, second, 32, &edited, &changed, &committed) !=
        FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(second);
    *second = edited;
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

static bool draw_group_fields(uint64_t handle, fdn_string* first, fdn_string* second) {
    const fdn_string group_name = {"edit-group", 10, 0};
    const fdn_string first_name = {"group-first", 11, 0};
    const fdn_string second_name = {"group-second", 12, 0};
    fdn_string edited = {0};
    bool changed = false;
    bool committed = false;
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 100.0f, 1);
    if (!foundation_ui_begin_compact_group(handle, &group_name))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_edit(handle, &first_name, first, 32, &edited, &changed, &committed) !=
        FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(first);
    *first = edited;
    edited = (fdn_string){0};
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_edit(handle, &second_name, second, 32, &edited, &changed, &committed) !=
        FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(second);
    *second = edited;
    foundation_ui_end_group(handle);
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

static bool draw_button_then_field(uint64_t handle, fdn_string* value, bool* pressed) {
    const fdn_string action = {"Navigate", 8, 0};
    const fdn_string name = {"navigation-field", 16, 0};
    fdn_string edited = {0};
    bool changed = false;
    bool committed = false;
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    *pressed = foundation_ui_action_button(handle, &action, FOUNDATION_UI_ACTION_SECONDARY, true);
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_edit(handle, &name, value, 32, &edited, &changed, &committed) !=
        FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(value);
    *value = edited;
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
}

static bool draw_action_field(uint64_t handle, fdn_string* value, bool enabled, bool* pressed) {
    const fdn_string name = {"action-field", 12, 0};
    const fdn_string hint = {"Folder", 6, 0};
    const fdn_string action = {"Choose...", 9, 0};
    fdn_string edited = {0};
    bool changed = false;
    bool committed = false;
    if (foundation_ui_begin_frame(handle) < 0 || !foundation_ui_begin_root(handle))
        return false;
    foundation_ui_row(handle, 40.0f, 1);
    if (foundation_ui_action_edit(handle, &name, value, &hint, &action,
                                  FOUNDATION_UI_ACTION_SECONDARY, 32, enabled, &edited, &changed,
                                  &committed, pressed) != FOUNDATION_UI_OK) {
        return false;
    }
    fdn_string_drop(value);
    *value = edited;
    foundation_ui_end_root(handle);
    return foundation_ui_end_frame(handle) == FOUNDATION_UI_OK;
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

static bool push_click_at(SDL_WindowID id, float x, float y) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = id;
    event.motion.x = x;
    event.motion.y = y;
    if (!SDL_PushEvent(&event))
        return false;
    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    if (!SDL_PushEvent(&event))
        return false;
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = false;
    return SDL_PushEvent(&event);
}

static bool push_text(SDL_WindowID id, const char* value) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = id;
    event.text.text = value;
    return SDL_PushEvent(&event);
}

static bool push_tab(SDL_WindowID id, bool down) {
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = id;
    event.key.down = down;
    event.key.key = SDLK_TAB;
    event.key.scancode = SDL_SCANCODE_TAB;
    return SDL_PushEvent(&event);
}

int main(void) {
    const fdn_string title = {"Edit focus", 10, 0};
    fdn_string first = {0};
    fdn_string second = {0};
    fdn_string navigation = {0};
    fdn_string group_first = {0};
    fdn_string group_second = {0};
    fdn_string action_value = {0};
    bool pressed = false;
    SDL_WindowID id;
    uint64_t handle = 0;
    int result = 0;
    if (foundation_ui_open(&title, 320, 200, &handle) != FOUNDATION_UI_OK ||
        foundation_ui_set_content_padding(handle, 0, 0) != FOUNDATION_UI_OK ||
        !draw_fields(handle, &first, &second)) {
        result = 1;
    }
    id = window_id();
    if (result == 0 && (!push_click(id, 20.0f) || !draw_fields(handle, &first, &second) ||
                        !push_text(id, "a") || !draw_fields(handle, &first, &second) ||
                        !push_click(id, 60.0f) || !draw_fields(handle, &first, &second) ||
                        !push_text(id, "b") || !draw_fields(handle, &first, &second))) {
        result = 2;
    }
    if (result == 0 && (first.length != 1 || memcmp(first.data, "a", 1) != 0 ||
                        second.length != 1 || memcmp(second.data, "b", 1) != 0)) {
        result = 3;
    }
    if (result == 0 &&
        (!push_click(id, 20.0f) || !draw_fields(handle, &first, &second) || !push_tab(id, true) ||
         !draw_fields(handle, &first, &second) || !push_tab(id, false) || !push_text(id, "c") ||
         !draw_fields(handle, &first, &second))) {
        result = 4;
    }
    if (result == 0 && (first.length != 1 || memcmp(first.data, "a", 1) != 0 ||
                        second.length != 2 || memcmp(second.data, "bc", 2) != 0)) {
        result = 5;
    }
    if (result == 0 &&
        (!draw_button_then_field(handle, &navigation, &pressed) || pressed ||
         !push_click(id, 60.0f) || !draw_button_then_field(handle, &navigation, &pressed) ||
         pressed || !push_click(id, 20.0f) ||
         !draw_button_then_field(handle, &navigation, &pressed) || !pressed)) {
        result = 6;
    }
    if (result == 0 && SDL_TextInputActive(SDL_GetWindowFromID(id))) {
        result = 7;
    }
    if (result == 0 &&
        (!draw_group_fields(handle, &group_first, &group_second) || !push_click(id, 20.0f) ||
         !draw_group_fields(handle, &group_first, &group_second))) {
        result = 8;
    }
    if (result == 0 && !SDL_TextInputActive(SDL_GetWindowFromID(id))) {
        result = 9;
    }
    if (result == 0 &&
        (!push_text(id, "g") || !draw_group_fields(handle, &group_first, &group_second) ||
         !push_click(id, 68.0f) || !draw_group_fields(handle, &group_first, &group_second) ||
         !push_text(id, "h") || !draw_group_fields(handle, &group_first, &group_second))) {
        result = 10;
    }
    if (result == 0 && (group_first.length != 1 || memcmp(group_first.data, "g", 1) != 0 ||
                        group_second.length != 1 || memcmp(group_second.data, "h", 1) != 0)) {
        result = 11;
    }
    if (result == 0 &&
        (!draw_action_field(handle, &action_value, true, &pressed) || pressed ||
         !push_click_at(id, 80.0f, 20.0f) ||
         !draw_action_field(handle, &action_value, true, &pressed) || !push_text(id, "j") ||
         !draw_action_field(handle, &action_value, true, &pressed) || pressed ||
         !push_click_at(id, 270.0f, 20.0f) ||
         !draw_action_field(handle, &action_value, true, &pressed) || !pressed)) {
        result = 12;
    }
    if (result == 0 && (action_value.length != 1 || memcmp(action_value.data, "j", 1) != 0)) {
        result = 13;
    }
    if (result == 0 && (!push_click_at(id, 270.0f, 20.0f) ||
                        !draw_action_field(handle, &action_value, false, &pressed) || pressed)) {
        result = 14;
    }
    fdn_string_drop(&first);
    fdn_string_drop(&second);
    fdn_string_drop(&navigation);
    fdn_string_drop(&group_first);
    fdn_string_drop(&group_second);
    fdn_string_drop(&action_value);
    foundation_ui_close(&handle);
    return result;
}
