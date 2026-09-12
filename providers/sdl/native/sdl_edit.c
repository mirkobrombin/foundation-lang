#include "sdl_ui_internal.h"

static foundation_ui_edit_state* foundation_ui_edit_for(foundation_ui* ui, const fdn_string* name,
                                                        uint64_t capacity, bool secret) {
    foundation_ui_edit_state* state;
    foundation_ui_edit_state* states;
    char* name_copy;
    char* buffer;
    char* mask;
    size_t next_capacity;
    size_t index;
    for (index = 0; index < ui->edit_count; index++) {
        state = &ui->edit_states[index];
        if (state->name_length == name->length &&
            SDL_memcmp(state->name, name->data, name->length) == 0) {
            state->secret = state->secret || secret;
            if (state->capacity >= capacity) {
                if (secret && state->mask == NULL) {
                    state->mask = SDL_calloc(state->capacity, 1);
                    if (state->mask == NULL)
                        return NULL;
                }
                return state;
            }
            if (state->secret) {
                buffer = SDL_calloc((size_t)capacity, 1);
                mask = SDL_calloc((size_t)capacity, 1);
                if (buffer == NULL || mask == NULL) {
                    SDL_free(buffer);
                    SDL_free(mask);
                    return NULL;
                }
                SDL_memcpy(buffer, state->buffer, state->capacity);
                if (state->mask != NULL)
                    SDL_memcpy(mask, state->mask, state->capacity);
                SDL_memset(state->buffer, 0, state->capacity);
                if (state->mask != NULL)
                    SDL_memset(state->mask, 0, state->capacity);
                SDL_free(state->buffer);
                SDL_free(state->mask);
                state->mask = mask;
            } else {
                buffer = SDL_realloc(state->buffer, (size_t)capacity);
                if (buffer == NULL)
                    return NULL;
                SDL_memset(buffer + state->capacity, 0, (size_t)(capacity - state->capacity));
            }
            state->buffer = buffer;
            state->capacity = capacity;
            return state;
        }
    }
    if (ui->edit_count == ui->edit_capacity) {
        next_capacity = ui->edit_capacity == 0 ? 4 : ui->edit_capacity * 2;
        if (next_capacity < ui->edit_capacity || next_capacity > SIZE_MAX / sizeof(*states))
            return NULL;
        states = SDL_realloc(ui->edit_states, next_capacity * sizeof(*states));
        if (states == NULL)
            return NULL;
        SDL_memset(states + ui->edit_capacity, 0,
                   (next_capacity - ui->edit_capacity) * sizeof(*states));
        ui->edit_states = states;
        ui->edit_capacity = next_capacity;
    }
    state = &ui->edit_states[ui->edit_count];
    name_copy = SDL_malloc(name->length + 1);
    if (name_copy == NULL)
        return NULL;
    buffer = SDL_calloc((size_t)capacity, 1);
    if (buffer == NULL) {
        SDL_free(name_copy);
        return NULL;
    }
    mask = secret ? SDL_calloc((size_t)capacity, 1) : NULL;
    if (secret && mask == NULL) {
        SDL_free(buffer);
        SDL_free(name_copy);
        return NULL;
    }
    SDL_memcpy(name_copy, name->data, name->length);
    name_copy[name->length] = '\0';
    state->name = name_copy;
    state->name_length = name->length;
    state->buffer = buffer;
    state->mask = mask;
    state->capacity = capacity;
    state->secret = secret;
    ui->edit_count++;
    return state;
}

static void foundation_ui_copy_selection(struct nk_context* context, struct nk_text_edit* edit) {
    nk_rune rune;
    int glyph_length;
    const int begin = NK_MIN(edit->select_start, edit->select_end);
    const int end = NK_MAX(edit->select_start, edit->select_end);
    const char* text;
    if (begin == end || context->clip.copy == NULL)
        return;
    text = nk_str_at_const(&edit->string, begin, &rune, &glyph_length);
    context->clip.copy(context->clip.userdata, text, end - begin);
}

static void foundation_ui_sync_edit(struct nk_window* window, struct nk_text_edit* edit,
                                    char* buffer, uint64_t capacity) {
    const nk_size length = edit->string.buffer.allocated;
    buffer[NK_MIN(length, (nk_size)capacity - 1)] = '\0';
    window->edit.cursor = edit->cursor;
    window->edit.sel_start = edit->select_start;
    window->edit.sel_end = edit->select_end;
    window->edit.mode = edit->mode;
    window->edit.scrollbar.x = (nk_uint)edit->scrollbar.x;
    window->edit.scrollbar.y = (nk_uint)edit->scrollbar.y;
}

static void foundation_ui_edit_menu(foundation_ui* ui, struct nk_window* window,
                                    struct nk_rect bounds, char* buffer, uint64_t capacity,
                                    bool secret) {
    struct nk_text_edit* edit = &ui->context->text_edit;
    const bool selected = edit->select_start != edit->select_end;
    const bool paste_available = SDL_HasClipboardText();
    bool changed = false;
    const float height = secret ? 84.0f : 152.0f;
    if (!nk_contextual_begin(ui->context, 0, nk_vec2(172.0f, height), bounds)) {
        return;
    }
    nk_layout_row_dynamic(ui->context, 32.0f, 1);
    if (!secret && !selected) {
        nk_widget_disable_begin(ui->context);
        ui->context->style.contextual_button.color_factor_background = 1.0f;
    }
    if (!secret) {
        if (nk_contextual_item_label(ui->context, "Cut", NK_TEXT_LEFT)) {
            foundation_ui_copy_selection(ui->context, edit);
            changed = nk_textedit_cut(edit);
        }
        if (nk_contextual_item_label(ui->context, "Copy", NK_TEXT_LEFT)) {
            foundation_ui_copy_selection(ui->context, edit);
        }
    }
    if (!secret && !selected)
        nk_widget_disable_end(ui->context);
    if (!paste_available) {
        nk_widget_disable_begin(ui->context);
        ui->context->style.contextual_button.color_factor_background = 1.0f;
    }
    if (nk_contextual_item_label(ui->context, "Paste", NK_TEXT_LEFT)) {
        ui->context->clip.paste(ui->context->clip.userdata, edit);
        changed = true;
    }
    if (!paste_available)
        nk_widget_disable_end(ui->context);
    if (nk_contextual_item_label(ui->context, "Select all", NK_TEXT_LEFT)) {
        nk_textedit_select_all(edit);
        changed = true;
    }
    if (changed)
        foundation_ui_sync_edit(window, edit, buffer, capacity);
    nk_contextual_end(ui->context);
}

static size_t foundation_ui_mask_secret(foundation_ui_edit_state* state) {
    const char* position = state->buffer;
    int remaining = (int)SDL_strlen(state->buffer);
    size_t length = 0;
    while (remaining > 0) {
        nk_rune rune;
        const int consumed = nk_utf_decode(position, &rune, remaining);
        if (consumed <= 0)
            break;
        state->mask[length++] = '*';
        position += consumed;
        remaining -= consumed;
    }
    state->mask[length] = '\0';
    return length;
}

static void foundation_ui_draw_secret(foundation_ui* ui, foundation_ui_edit_state* state,
                                      struct nk_rect bounds, struct nk_rect clip) {
    struct nk_command_buffer* canvas = nk_window_get_canvas(ui->context);
    const struct nk_style_edit* style = &ui->context->style.edit;
    const size_t length = foundation_ui_mask_secret(state);
    struct nk_rect visible;
    struct nk_rect area = nk_rect(bounds.x + style->padding.x + style->border,
                                  bounds.y + style->padding.y + style->border,
                                  bounds.w - 2.0f * (style->padding.x + style->border),
                                  bounds.h - 2.0f * (style->padding.y + style->border));
    area.x -= (float)ui->context->current->edit.scrollbar.x;
    visible.x = NK_MAX(clip.x, bounds.x);
    visible.y = NK_MAX(clip.y, bounds.y);
    visible.w = NK_MAX(0.0f, NK_MIN(clip.x + clip.w, bounds.x + bounds.w) - visible.x);
    visible.h = NK_MAX(0.0f, NK_MIN(clip.y + clip.h, bounds.y + bounds.h) - visible.y);
    nk_push_scissor(canvas, visible);
    nk_draw_text(canvas, area, state->mask, (int)length, &ui->terminal_font->handle,
                 nk_rgba(0, 0, 0, 0), ui->text);
    nk_push_scissor(canvas, clip);
}

static void foundation_ui_draw_edit_hint(foundation_ui* ui, const fdn_string* hint,
                                         struct nk_rect bounds, struct nk_rect clip) {
    struct nk_command_buffer* canvas = nk_window_get_canvas(ui->context);
    const struct nk_style_edit* style = &ui->context->style.edit;
    struct nk_rect visible;
    const struct nk_rect area = nk_rect(
        bounds.x + style->padding.x + style->border,
        bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
        bounds.w - 2.0f * (style->padding.x + style->border), ui->context->style.font->height);
    visible.x = NK_MAX(clip.x, bounds.x);
    visible.y = NK_MAX(clip.y, bounds.y);
    visible.w = NK_MAX(0.0f, NK_MIN(clip.x + clip.w, bounds.x + bounds.w) - visible.x);
    visible.h = NK_MAX(0.0f, NK_MIN(clip.y + clip.h, bounds.y + bounds.h) - visible.y);
    nk_push_scissor(canvas, visible);
    nk_draw_text(canvas, area, foundation_ui_string_data(hint), (int)hint->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), ui->muted);
    nk_push_scissor(canvas, clip);
}

static bool foundation_ui_edit_click(const struct nk_input* input, enum nk_buttons button,
                                     struct nk_rect bounds) {
    return input->mouse.buttons[button].clicked != 0 &&
           nk_input_has_mouse_click_in_rect(input, button, bounds);
}

static int32_t foundation_ui_edit_value_at(uint64_t handle, const fdn_string* name,
                                           const fdn_string* value, const fdn_string* hint,
                                           uint64_t capacity, fdn_string* result, bool* changed,
                                           bool* committed, bool secret,
                                           const struct nk_rect* supplied_bounds) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_edit_state* state;
    uint64_t length;
    nk_flags flags;
    struct nk_rect bounds;
    struct nk_rect clip;
    struct nk_window* window;
    struct nk_style_edit saved_style;
    const struct nk_user_font* saved_font;
    nk_plugin_copy saved_copy;
    size_t widget_index;
    bool was_focused;
    bool clicked;
    bool clicked_inside;
    if (ui == NULL || !foundation_ui_string_valid(name) || !foundation_ui_string_valid(value) ||
        (hint != NULL && (!foundation_ui_string_valid(hint) || hint->length > INT32_MAX)) ||
        result == NULL || changed == NULL || committed == NULL || name->length == 0 ||
        name->length == SIZE_MAX || memchr(name->data, '\0', name->length) != NULL ||
        (value->length != 0 && memchr(value->data, '\0', value->length) != NULL) || capacity == 0 ||
        capacity > INT32_MAX || value->length >= capacity) {
        return FOUNDATION_UI_INVALID;
    }
    state = foundation_ui_edit_for(ui, name, capacity, secret);
    if (state == NULL)
        return FOUNDATION_UI_FAILED;
    length = SDL_strlen(state->buffer);
    if (length != value->length ||
        (length != 0 && SDL_memcmp(state->buffer, value->data, length) != 0)) {
        if (state->secret)
            SDL_memset(state->buffer, 0, state->capacity);
        if (value->length != 0)
            SDL_memcpy(state->buffer, value->data, value->length);
        state->buffer[value->length] = '\0';
    }
    flags = (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_CLIPBOARD | (nk_flags)NK_EDIT_SIG_ENTER;
    bounds = supplied_bounds == NULL ? nk_widget_bounds(ui->context) : *supplied_bounds;
    window = ui->context->current;
    clip = window->buffer.clip;
    widget_index = ui->edit_widget_count++;
    was_focused = window->edit.active && window->edit.name == window->edit.seq;
    clicked = ui->context->input.mouse.buttons[NK_BUTTON_LEFT].clicked != 0 ||
              ui->context->input.mouse.buttons[NK_BUTTON_RIGHT].clicked != 0;
    clicked_inside = foundation_ui_edit_click(&ui->context->input, NK_BUTTON_LEFT, bounds) ||
                     foundation_ui_edit_click(&ui->context->input, NK_BUTTON_RIGHT, bounds);
    if (clicked)
        ui->edit_focus_requested = false;
    if (clicked_inside || (ui->edit_focus_requested && ui->edit_focus_target == widget_index)) {
        nk_edit_focus(ui->context, flags);
        ui->edit_focus_requested = false;
    } else if (clicked && was_focused) {
        nk_edit_unfocus(ui->context);
    }
    saved_style = ui->context->style.edit;
    saved_font = ui->context->style.font;
    saved_copy = ui->context->clip.copy;
    if (secret) {
        const struct nk_color hidden = nk_rgba(0, 0, 0, 0);
        ui->context->style.font = &ui->terminal_font->handle;
        ui->context->style.edit.text_normal = hidden;
        ui->context->style.edit.text_hover = hidden;
        ui->context->style.edit.text_active = hidden;
        ui->context->style.edit.selected_text_normal = hidden;
        ui->context->style.edit.selected_text_hover = hidden;
        ui->context->style.edit.cursor_text_normal = hidden;
        ui->context->style.edit.cursor_text_hover = hidden;
        ui->context->clip.copy = NULL;
    }
    flags = supplied_bounds == NULL
                ? nk_edit_string_zero_terminated(ui->context, flags, state->buffer,
                                                 (int)state->capacity, nk_filter_default)
                : foundation_ui_edit_string_bounds(ui->context, bounds, flags, state->buffer,
                                                   (int)state->capacity);
    if (was_focused && nk_input_is_key_pressed(&ui->context->input, NK_KEY_TAB)) {
        const bool backward = ui->context->input.keyboard.keys[NK_KEY_SHIFT].down != 0;
        const size_t count = ui->previous_edit_widget_count;
        if (backward) {
            ui->edit_focus_target =
                widget_index == 0 ? (count == 0 ? 0 : count - 1) : widget_index - 1;
        } else {
            ui->edit_focus_target = count != 0 && widget_index + 1 >= count ? 0 : widget_index + 1;
        }
        ui->edit_focus_requested = true;
        nk_edit_unfocus(ui->context);
    }
    if (secret) {
        ui->context->style.edit = saved_style;
        ui->context->style.font = saved_font;
        ui->context->clip.copy = saved_copy;
        foundation_ui_draw_secret(ui, state, bounds, clip);
    } else if (state->buffer[0] == '\0' && hint != NULL && hint->length != 0) {
        foundation_ui_draw_edit_hint(ui, hint, bounds, clip);
    }
    foundation_ui_edit_menu(ui, window, bounds, state->buffer, state->capacity, secret);
    fdn_string_drop(result);
    *result =
        foundation_runtime_string_copy(&(fdn_string){state->buffer, SDL_strlen(state->buffer), 0});
    *changed = (flags & NK_EDIT_COMMITED) != 0 || result->length != value->length ||
               (result->length != 0 && SDL_memcmp(result->data, value->data, result->length) != 0);
    *committed = (flags & NK_EDIT_COMMITED) != 0;
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                           uint64_t capacity, fdn_string* result, bool* changed, bool* committed) {
    return foundation_ui_edit_value_at(handle, name, value, NULL, capacity, result, changed,
                                       committed, false, NULL);
}

int32_t foundation_ui_edit_hint(uint64_t handle, const fdn_string* name, const fdn_string* value,
                                const fdn_string* hint, uint64_t capacity, fdn_string* result,
                                bool* changed, bool* committed) {
    return foundation_ui_edit_value_at(handle, name, value, hint, capacity, result, changed,
                                       committed, false, NULL);
}

int32_t foundation_ui_action_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                                  const fdn_string* hint, const fdn_string* action, uint64_t style,
                                  uint64_t capacity, bool enabled, fdn_string* result,
                                  bool* changed, bool* committed, bool* activated) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_command_buffer* canvas;
    struct nk_style_edit saved_style;
    struct nk_rect bounds;
    struct nk_rect edit_bounds;
    struct nk_rect action_bounds;
    struct nk_rect action_paint;
    struct nk_rect clip;
    struct nk_color transparent = nk_rgba(0, 0, 0, 0);
    nk_flags action_state = 0;
    float action_width;
    int32_t status;
    if (ui == NULL || result == NULL || changed == NULL || committed == NULL || activated == NULL ||
        !foundation_ui_string_valid(name) || !foundation_ui_string_valid(value) ||
        !foundation_ui_string_valid(hint) || !foundation_ui_string_valid(action) ||
        name->length == 0 || name->length == SIZE_MAX || name->length > INT32_MAX ||
        value->length > INT32_MAX || hint->length > INT32_MAX || action->length > INT32_MAX ||
        capacity == 0 || capacity > INT32_MAX || value->length >= capacity ||
        style > FOUNDATION_UI_ACTION_DESTRUCTIVE ||
        memchr(name->data, '\0', name->length) != NULL ||
        (value->length != 0 && memchr(value->data, '\0', value->length) != NULL)) {
        return FOUNDATION_UI_INVALID;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID) {
        fdn_string_drop(result);
        *result = foundation_runtime_string_copy(value);
        *changed = false;
        *committed = false;
        *activated = false;
        return FOUNDATION_UI_OK;
    }
    font = ui->context->style.font;
    action_width = font->width(font->userdata, font->height, foundation_ui_string_data(action),
                               (int)action->length) +
                   32.0f;
    if (action_width < 112.0f)
        action_width = 112.0f;
    if (action_width > bounds.w * 0.40f)
        action_width = bounds.w * 0.40f;
    if (action_width < 1.0f || bounds.w - action_width < 1.0f)
        return FOUNDATION_UI_FAILED;
    edit_bounds = nk_rect(bounds.x, bounds.y, bounds.w - action_width, bounds.h);
    action_bounds = nk_rect(edit_bounds.x + edit_bounds.w, bounds.y, action_width, bounds.h);
    foundation_ui_set_context_target(ui, bounds);
    *activated =
        enabled && foundation_ui_button_input(&action_state, action_bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, bounds, 6.0f, ui->panel);
    nk_stroke_rect(canvas, bounds, 6.0f, 1.0f, ui->context->style.window.border_color);
    saved_style = ui->context->style.edit;
    ui->context->style.edit.normal = nk_style_item_color(transparent);
    ui->context->style.edit.hover = nk_style_item_color(transparent);
    ui->context->style.edit.active = nk_style_item_color(transparent);
    ui->context->style.edit.border = 0.0f;
    ui->context->style.edit.rounding = 0.0f;
    status = foundation_ui_edit_value_at(handle, name, value, hint, capacity, result, changed,
                                         committed, false, &edit_bounds);
    ui->context->style.edit = saved_style;
    if (status != FOUNDATION_UI_OK)
        return status;
    clip = canvas->clip;
    nk_push_scissor(canvas, action_bounds);
    action_paint =
        nk_rect(action_bounds.x - 6.0f, action_bounds.y, action_bounds.w + 6.0f, action_bounds.h);
    foundation_ui_draw_action(ui, action_paint, action, style, enabled, action_state);
    nk_push_scissor(canvas, clip);
    nk_stroke_line(canvas, action_bounds.x, action_bounds.y + 1.0f, action_bounds.x,
                   action_bounds.y + action_bounds.h - 1.0f, 1.0f,
                   ui->context->style.window.border_color);
    foundation_ui_set_context_target(ui, bounds);
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_secret_edit(uint64_t handle, const fdn_string* name, const fdn_string* value,
                                  uint64_t capacity, fdn_string* result, bool* changed,
                                  bool* committed) {
    return foundation_ui_edit_value_at(handle, name, value, NULL, capacity, result, changed,
                                       committed, true, NULL);
}
