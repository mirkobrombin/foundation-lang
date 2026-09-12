#include "sdl_ui_internal.h"

static nk_byte foundation_ui_shift_channel(nk_byte value, int amount) {
    const int shifted = (int)value + amount;
    if (shifted < 0)
        return 0;
    if (shifted > UINT8_MAX)
        return UINT8_MAX;
    return (nk_byte)shifted;
}

static struct nk_color foundation_ui_shift_color(struct nk_color value, int amount) {
    return nk_rgba(foundation_ui_shift_channel(value.r, amount),
                   foundation_ui_shift_channel(value.g, amount),
                   foundation_ui_shift_channel(value.b, amount), value.a);
}

bool foundation_ui_button_input(nk_flags* state, struct nk_rect bounds,
                                const struct nk_input* input) {
    bool pressed = false;
    *state = 0;
    if (input == NULL)
        return false;
    if (nk_input_is_mouse_hovering_rect(input, bounds)) {
        *state = NK_WIDGET_STATE_HOVERED;
        if (nk_input_is_mouse_down(input, NK_BUTTON_LEFT))
            *state = NK_WIDGET_STATE_ACTIVE;
        if (nk_input_has_mouse_click_in_button_rect(input, NK_BUTTON_LEFT, bounds))
            pressed = nk_input_is_mouse_released(input, NK_BUTTON_LEFT);
    }
    if ((*state & NK_WIDGET_STATE_HOVER) != 0 &&
        !nk_input_is_mouse_prev_hovering_rect(input, bounds)) {
        *state |= NK_WIDGET_STATE_ENTERED;
    } else if (nk_input_is_mouse_prev_hovering_rect(input, bounds)) {
        *state |= NK_WIDGET_STATE_LEFT;
    }
    return pressed;
}

void foundation_ui_set_context_target(foundation_ui* ui, struct nk_rect bounds) {
    if (ui == NULL || !isfinite(bounds.x) || !isfinite(bounds.y) || !isfinite(bounds.w) ||
        !isfinite(bounds.h) || bounds.w <= 0.0f || bounds.h <= 0.0f) {
        return;
    }
    ui->context_target = bounds;
    ui->context_target_valid = true;
}

uint8_t* foundation_ui_reserve_image(foundation_ui* ui, foundation_ui_texture* image,
                                     uint64_t width, uint64_t height, uint64_t* capacity) {
    SDL_Texture* texture;
    uint8_t* storage;
    uint64_t length;
    if (ui == NULL || image == NULL || capacity == NULL || width == 0 || height == 0 ||
        width > INT32_MAX || height > INT32_MAX || width > UINT64_MAX / height ||
        width * height > UINT64_MAX / 4) {
        return NULL;
    }
    length = width * height * 4;
    if (length > SIZE_MAX)
        return NULL;
    if (image->texture == NULL || image->width != width || image->height != height) {
        texture = SDL_CreateTexture(ui->renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);
        if (texture == NULL)
            return NULL;
        if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
            SDL_DestroyTexture(texture);
            return NULL;
        }
        storage = SDL_realloc(image->pixels, (size_t)length);
        if (storage == NULL) {
            SDL_DestroyTexture(texture);
            return NULL;
        }
        SDL_DestroyTexture(image->texture);
        image->texture = texture;
        image->pixels = storage;
        image->capacity = length;
        image->width = width;
        image->height = height;
    }
    *capacity = image->capacity;
    return image->pixels;
}

int32_t foundation_ui_commit_image(foundation_ui_texture* image) {
    if (image == NULL || image->texture == NULL || image->pixels == NULL ||
        image->width > INT32_MAX / 4) {
        return FOUNDATION_UI_INVALID;
    }
    if (!SDL_UpdateTexture(image->texture, NULL, image->pixels, (int)(image->width * 4))) {
        return FOUNDATION_UI_FAILED;
    }
    return FOUNDATION_UI_OK;
}

uint8_t* foundation_ui_application_icon_buffer(uint64_t handle, uint64_t width, uint64_t height,
                                               uint64_t* capacity) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL)
        return NULL;
    return foundation_ui_reserve_image(ui, &ui->application_image, width, height, capacity);
}

int32_t foundation_ui_application_icon_commit(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    SDL_Surface* surface;
    int32_t status;
    if (ui == NULL)
        return FOUNDATION_UI_INVALID;
    status = foundation_ui_commit_image(&ui->application_image);
    if (status != FOUNDATION_UI_OK)
        return status;
    surface = SDL_CreateSurfaceFrom(
        (int)ui->application_image.width, (int)ui->application_image.height, SDL_PIXELFORMAT_RGBA32,
        ui->application_image.pixels, (int)(ui->application_image.width * 4));
    if (surface == NULL)
        return FOUNDATION_UI_FAILED;
    const bool applied = SDL_SetWindowIcon(ui->window, surface);
    SDL_DestroySurface(surface);
    return applied ? FOUNDATION_UI_OK : FOUNDATION_UI_FAILED;
}

void foundation_ui_application_icon(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_command_buffer* canvas;
    struct nk_image image;
    if (ui == NULL || ui->application_image.texture == NULL)
        return;
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return;
    foundation_ui_set_context_target(ui, bounds);
    canvas = nk_window_get_canvas(ui->context);
    image = nk_image_ptr(ui->application_image.texture);
    bounds.x += (bounds.w - 40.0f) * 0.5f;
    bounds.y += (bounds.h - 40.0f) * 0.5f;
    bounds.w = 40.0f;
    bounds.h = 40.0f;
    nk_draw_image(canvas, bounds, &image, nk_rgb(255, 255, 255));
}

void foundation_ui_heading(uint64_t handle, const fdn_string* value) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* previous;
    struct nk_rect bounds;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX)
        return;
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    previous = ui->context->style.font;
    nk_style_set_font(ui->context, &ui->heading_font->handle);
    nk_text(ui->context, foundation_ui_string_data(value), (int)value->length, NK_TEXT_LEFT);
    nk_style_set_font(ui->context, previous);
}

void foundation_ui_label(uint64_t handle, const fdn_string* value, uint64_t tone, bool wrap) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect label;
    struct nk_command_buffer* canvas;
    struct nk_color previous;
    struct nk_color selected;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX)
        return;
    previous = ui->context->style.text.color;
    selected = previous;
    if (tone == FOUNDATION_UI_LABEL_MUTED)
        selected = ui->muted;
    if (tone == FOUNDATION_UI_LABEL_ACCENT)
        selected = ui->accent;
    if (tone == FOUNDATION_UI_LABEL_DANGER)
        selected = nk_rgb(255, 112, 112);
    ui->context->style.text.color = selected;
    if (wrap) {
        bounds = nk_widget_bounds(ui->context);
        foundation_ui_set_context_target(ui, bounds);
        nk_text_wrap(ui->context, foundation_ui_string_data(value), (int)value->length);
    } else {
        if (nk_widget(&bounds, ui->context) != NK_WIDGET_INVALID) {
            foundation_ui_set_context_target(ui, bounds);
            label =
                nk_rect(bounds.x, bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
                        bounds.w, ui->context->style.font->height);
            canvas = nk_window_get_canvas(ui->context);
            nk_draw_text(canvas, label, foundation_ui_string_data(value), (int)value->length,
                         ui->context->style.font, nk_rgba(0, 0, 0, 0), selected);
        }
    }
    ui->context->style.text.color = previous;
}

bool foundation_ui_button(uint64_t handle, const fdn_string* value, bool selected, bool primary) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_style_button previous;
    bool pressed;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX)
        return false;
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    previous = ui->context->style.button;
    if (primary || selected) {
        ui->context->style.button.normal = nk_style_item_color(ui->accent);
        ui->context->style.button.hover =
            nk_style_item_color(foundation_ui_shift_color(ui->accent, 16));
        ui->context->style.button.active =
            nk_style_item_color(foundation_ui_shift_color(ui->accent, -24));
        ui->context->style.button.text_normal = nk_rgb(9, 12, 18);
        ui->context->style.button.text_hover = nk_rgb(9, 12, 18);
        ui->context->style.button.text_active = nk_rgb(9, 12, 18);
    }
    pressed = nk_button_text(ui->context, foundation_ui_string_data(value), (int)value->length);
    ui->context->style.button = previous;
    return pressed;
}

void foundation_ui_draw_action(foundation_ui* ui, struct nk_rect bounds, const fdn_string* value,
                               uint64_t style, bool enabled, nk_flags state) {
    const struct nk_user_font* font;
    struct nk_rect label;
    struct nk_command_buffer* canvas;
    struct nk_color fill;
    struct nk_color foreground;
    float text_width;
    float available_width;
    fill = ui->raised;
    foreground = ui->text;
    if (!enabled) {
        fill = ui->raised;
        foreground = ui->muted;
    } else if (style == FOUNDATION_UI_ACTION_PRIMARY) {
        fill = ui->accent;
        foreground = nk_rgb(9, 12, 18);
    } else if (style == FOUNDATION_UI_ACTION_DESTRUCTIVE) {
        fill = nk_rgb(184, 73, 80);
        foreground = nk_rgb(255, 255, 255);
    }
    if (enabled && (state & NK_WIDGET_STATE_HOVER) != 0)
        fill = foundation_ui_shift_color(fill, 16);
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, bounds, 6.0f, fill);
    nk_stroke_rect(canvas, bounds, 6.0f, 1.0f,
                   enabled && style == FOUNDATION_UI_ACTION_PRIMARY
                       ? ui->accent
                       : ui->context->style.window.border_color);
    font = ui->context->style.font;
    text_width = font->width(font->userdata, font->height, foundation_ui_string_data(value),
                             (int)value->length);
    available_width = bounds.w > 24.0f ? bounds.w - 24.0f : 0.0f;
    if (text_width > available_width)
        text_width = available_width;
    label = nk_rect(bounds.x + (bounds.w - text_width) * 0.5f,
                    bounds.y + (bounds.h - font->height) * 0.5f, text_width, font->height);
    nk_draw_text(canvas, label, foundation_ui_string_data(value), (int)value->length, font,
                 nk_rgba(0, 0, 0, 0), foreground);
}

bool foundation_ui_action_button(uint64_t handle, const fdn_string* value, uint64_t style,
                                 bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX ||
        style > FOUNDATION_UI_ACTION_DESTRUCTIVE) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    foundation_ui_draw_action(ui, bounds, value, style, enabled, state);
    return pressed;
}

static void foundation_ui_draw_switch(foundation_ui* ui, struct nk_rect bounds, bool value,
                                      bool enabled, nk_flags state) {
    struct nk_rect track;
    struct nk_rect knob;
    struct nk_command_buffer* canvas;
    struct nk_color track_color;
    struct nk_color knob_color;
    track = nk_rect(bounds.x, bounds.y + (bounds.h - 22.0f) * 0.5f, 40.0f, 22.0f);
    track_color = value ? ui->accent : ui->raised;
    if ((state & NK_WIDGET_STATE_HOVER) != 0)
        track_color = foundation_ui_shift_color(track_color, 16);
    if (!enabled)
        track_color.a = 110;
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, track, 11.0f, track_color);
    if (!value)
        nk_stroke_rect(canvas, track, 11.0f, 1.0f, ui->context->style.window.border_color);
    knob = nk_rect(value ? track.x + 21.0f : track.x + 3.0f, track.y + 3.0f, 16.0f, 16.0f);
    knob_color = value ? nk_rgb(9, 12, 18) : ui->muted;
    if (!enabled)
        knob_color.a = 110;
    nk_fill_circle(canvas, knob, knob_color);
}

bool foundation_ui_switch(uint64_t handle, bool value, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect track;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL)
        return false;
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    track = nk_rect(bounds.x, bounds.y + (bounds.h - 22.0f) * 0.5f, 40.0f, 22.0f);
    foundation_ui_set_context_target(ui, track);
    if (enabled)
        pressed = foundation_ui_button_input(&state, track, &ui->context->input);
    foundation_ui_draw_switch(ui, bounds, value, enabled, state);
    return pressed;
}

bool foundation_ui_toggle_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                               bool value, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect text_bounds;
    struct nk_rect switch_bounds;
    struct nk_command_buffer* canvas;
    struct nk_color title_color;
    struct nk_color detail_color;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        title->length > INT32_MAX || detail->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if ((state & NK_WIDGET_STATE_HOVER) != 0)
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    title_color = ui->text;
    detail_color = ui->muted;
    if (!enabled) {
        title_color.a = 110;
        detail_color.a = 110;
    }
    text_bounds = nk_rect(bounds.x + 12.0f, bounds.y + 6.0f, bounds.w - 76.0f,
                          ui->context->style.font->height);
    nk_draw_text(canvas, text_bounds, foundation_ui_string_data(title), (int)title->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), title_color);
    if (detail->length != 0) {
        text_bounds.y = bounds.y + bounds.h - ui->context->style.font->height - 6.0f;
        nk_draw_text(canvas, text_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     ui->context->style.font, nk_rgba(0, 0, 0, 0), detail_color);
    }
    switch_bounds = nk_rect(bounds.x + bounds.w - 52.0f, bounds.y, 40.0f, bounds.h);
    foundation_ui_draw_switch(ui, switch_bounds, value, enabled, state);
    return pressed;
}

static bool foundation_ui_selection_item(uint64_t handle, const fdn_string* title,
                                         const fdn_string* detail, bool value, bool enabled,
                                         bool radio) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect text_bounds;
    struct nk_rect mark;
    struct nk_command_buffer* canvas;
    struct nk_color title_color;
    struct nk_color detail_color;
    struct nk_color mark_color;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        title->length > INT32_MAX || detail->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if ((state & NK_WIDGET_STATE_HOVER) != 0)
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    title_color = ui->text;
    detail_color = ui->muted;
    mark_color = value ? ui->accent : ui->context->style.window.border_color;
    if (!enabled) {
        title_color.a = 110;
        detail_color.a = 110;
        mark_color.a = 110;
    }
    text_bounds = nk_rect(bounds.x + 12.0f, bounds.y + 6.0f, bounds.w - 64.0f,
                          ui->context->style.font->height);
    nk_draw_text(canvas, text_bounds, foundation_ui_string_data(title), (int)title->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), title_color);
    if (detail->length != 0) {
        text_bounds.y = bounds.y + bounds.h - ui->context->style.font->height - 6.0f;
        nk_draw_text(canvas, text_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     ui->context->style.font, nk_rgba(0, 0, 0, 0), detail_color);
    }
    mark = nk_rect(bounds.x + bounds.w - 34.0f, bounds.y + (bounds.h - 18.0f) * 0.5f, 18.0f, 18.0f);
    if (radio) {
        if (value) {
            nk_fill_circle(canvas, mark, mark_color);
            nk_fill_circle(canvas, nk_rect(mark.x + 5.0f, mark.y + 5.0f, 8.0f, 8.0f),
                           nk_rgb(9, 12, 18));
        } else {
            nk_stroke_circle(canvas, mark, 1.5f, mark_color);
        }
    } else if (value) {
        nk_fill_rect(canvas, mark, 4.0f, mark_color);
        nk_stroke_line(canvas, mark.x + 4.0f, mark.y + 9.0f, mark.x + 8.0f, mark.y + 13.0f, 1.8f,
                       nk_rgb(9, 12, 18));
        nk_stroke_line(canvas, mark.x + 8.0f, mark.y + 13.0f, mark.x + 15.0f, mark.y + 5.0f, 1.8f,
                       nk_rgb(9, 12, 18));
    } else {
        nk_stroke_rect(canvas, mark, 4.0f, 1.5f, mark_color);
    }
    return pressed;
}

bool foundation_ui_check_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                              bool value, bool enabled) {
    return foundation_ui_selection_item(handle, title, detail, value, enabled, false);
}

bool foundation_ui_radio_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                              bool selected, bool enabled) {
    return foundation_ui_selection_item(handle, title, detail, selected, enabled, true);
}

bool foundation_ui_action_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                               const fdn_string* action, uint64_t style, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect text_bounds;
    struct nk_rect action_bounds;
    struct nk_command_buffer* canvas;
    struct nk_color title_color;
    struct nk_color detail_color;
    nk_flags state = 0;
    float action_width;
    float action_height;
    float text_width;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        !foundation_ui_string_valid(action) || title->length > INT32_MAX ||
        detail->length > INT32_MAX || action->length > INT32_MAX ||
        style > FOUNDATION_UI_ACTION_DESTRUCTIVE) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    font = ui->context->style.font;
    action_width = font->width(font->userdata, font->height, foundation_ui_string_data(action),
                               (int)action->length) +
                   32.0f;
    if (action_width < 96.0f)
        action_width = 96.0f;
    if (action_width > bounds.w * 0.40f)
        action_width = bounds.w * 0.40f;
    action_height = bounds.h > 14.0f ? bounds.h - 14.0f : bounds.h;
    action_bounds =
        nk_rect(bounds.x + bounds.w - action_width - 12.0f,
                bounds.y + (bounds.h - action_height) * 0.5f, action_width, action_height);
    foundation_ui_set_context_target(ui, action_bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, action_bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    title_color = ui->text;
    detail_color = ui->muted;
    if (!enabled) {
        title_color.a = 110;
        detail_color.a = 110;
    }
    text_width = bounds.w - action_width - 48.0f;
    if (text_width < 0.0f)
        text_width = 0.0f;
    text_bounds = nk_rect(bounds.x + 12.0f, bounds.y + 6.0f, text_width, font->height);
    nk_draw_text(canvas, text_bounds, foundation_ui_string_data(title), (int)title->length, font,
                 nk_rgba(0, 0, 0, 0), title_color);
    if (detail->length != 0) {
        text_bounds.y = bounds.y + bounds.h - font->height - 6.0f;
        nk_draw_text(canvas, text_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     font, nk_rgba(0, 0, 0, 0), detail_color);
    }
    foundation_ui_draw_action(ui, action_bounds, action, style, enabled, state);
    return pressed;
}

bool foundation_ui_choice_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                               const fdn_string* value, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect title_bounds;
    struct nk_rect detail_bounds;
    struct nk_rect value_bounds;
    struct nk_command_buffer* canvas;
    struct nk_color title_color;
    struct nk_color detail_color;
    nk_flags state = 0;
    float value_width;
    float text_width;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        !foundation_ui_string_valid(value) || title->length > INT32_MAX ||
        detail->length > INT32_MAX || value->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if ((state & NK_WIDGET_STATE_HOVER) != 0)
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    title_color = ui->text;
    detail_color = ui->muted;
    if (!enabled) {
        title_color.a = 110;
        detail_color.a = 110;
    }
    font = ui->context->style.font;
    value_width = bounds.w * 0.36f;
    if (value_width > 240.0f)
        value_width = 240.0f;
    if (value_width < 72.0f)
        value_width = 72.0f;
    text_width = bounds.w - value_width - 48.0f;
    if (text_width < 0.0f)
        text_width = 0.0f;
    title_bounds = nk_rect(bounds.x + 12.0f, bounds.y + 6.0f, text_width, font->height);
    nk_draw_text(canvas, title_bounds, foundation_ui_string_data(title), (int)title->length, font,
                 nk_rgba(0, 0, 0, 0), title_color);
    if (detail->length != 0) {
        detail_bounds = nk_rect(bounds.x + 12.0f, bounds.y + bounds.h - font->height - 6.0f,
                                text_width, font->height);
        nk_draw_text(canvas, detail_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     font, nk_rgba(0, 0, 0, 0), detail_color);
    }
    value_bounds = nk_rect(bounds.x + bounds.w - value_width - 28.0f,
                           bounds.y + (bounds.h - font->height) * 0.5f, value_width, font->height);
    nk_draw_text(canvas, value_bounds, foundation_ui_string_data(value), (int)value->length, font,
                 nk_rgba(0, 0, 0, 0), detail_color);
    nk_stroke_line(canvas, bounds.x + bounds.w - 18.0f, bounds.y + bounds.h * 0.5f - 3.0f,
                   bounds.x + bounds.w - 14.0f, bounds.y + bounds.h * 0.5f + 1.0f, 1.5f,
                   detail_color);
    nk_stroke_line(canvas, bounds.x + bounds.w - 14.0f, bounds.y + bounds.h * 0.5f + 1.0f,
                   bounds.x + bounds.w - 10.0f, bounds.y + bounds.h * 0.5f - 3.0f, 1.5f,
                   detail_color);
    return pressed;
}

void foundation_ui_property_item(uint64_t handle, const fdn_string* title,
                                 const fdn_string* value) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect title_bounds;
    struct nk_rect value_bounds;
    struct nk_command_buffer* canvas;
    float split;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(value) ||
        title->length > INT32_MAX || value->length > INT32_MAX) {
        return;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return;
    foundation_ui_set_context_target(ui, bounds);
    split = bounds.w * 0.32f;
    if (split < 120.0f)
        split = 120.0f;
    if (split > 240.0f)
        split = 240.0f;
    font = ui->context->style.font;
    title_bounds = nk_rect(bounds.x + 12.0f, bounds.y + (bounds.h - font->height) * 0.5f,
                           split - 20.0f, font->height);
    value_bounds = nk_rect(bounds.x + split, bounds.y + (bounds.h - font->height) * 0.5f,
                           bounds.w - split - 12.0f, font->height);
    canvas = nk_window_get_canvas(ui->context);
    nk_draw_text(canvas, title_bounds, foundation_ui_string_data(title), (int)title->length, font,
                 nk_rgba(0, 0, 0, 0), ui->muted);
    nk_draw_text(canvas, value_bounds, foundation_ui_string_data(value), (int)value->length, font,
                 nk_rgba(0, 0, 0, 0), ui->text);
}

void foundation_ui_empty_state(uint64_t handle, uint64_t icon, const fdn_string* title,
                               const fdn_string* detail) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect icon_bounds;
    struct nk_rect title_bounds;
    struct nk_rect detail_bounds;
    struct nk_command_buffer* canvas;
    float title_width;
    float detail_width;
    float center_y;
    if (ui == NULL || icon > FOUNDATION_UI_ICON_ACCESSIBILITY ||
        !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        title->length > INT32_MAX || detail->length > INT32_MAX) {
        return;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return;
    foundation_ui_set_context_target(ui, bounds);
    if (bounds.w < 48.0f || bounds.h < 80.0f)
        return;
    canvas = nk_window_get_canvas(ui->context);
    font = ui->context->style.font;
    center_y = bounds.y + bounds.h * 0.44f;
    icon_bounds = nk_rect(bounds.x + (bounds.w - 36.0f) * 0.5f, center_y - 48.0f, 36.0f, 36.0f);
    foundation_ui_draw_icon(canvas, icon_bounds, icon, ui->muted);
    title_width = font->width(font->userdata, font->height, foundation_ui_string_data(title),
                              (int)title->length);
    if (title_width > bounds.w - 24.0f)
        title_width = bounds.w - 24.0f;
    title_bounds =
        nk_rect(bounds.x + (bounds.w - title_width) * 0.5f, center_y, title_width, font->height);
    nk_draw_text(canvas, title_bounds, foundation_ui_string_data(title), (int)title->length, font,
                 nk_rgba(0, 0, 0, 0), ui->text);
    detail_width = font->width(font->userdata, font->height, foundation_ui_string_data(detail),
                               (int)detail->length);
    if (detail_width > bounds.w - 24.0f)
        detail_width = bounds.w - 24.0f;
    detail_bounds = nk_rect(bounds.x + (bounds.w - detail_width) * 0.5f,
                            center_y + font->height + 6.0f, detail_width, font->height);
    nk_draw_text(canvas, detail_bounds, foundation_ui_string_data(detail), (int)detail->length,
                 font, nk_rgba(0, 0, 0, 0), ui->muted);
}

void foundation_ui_notice(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                          uint64_t tone) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect title_bounds;
    struct nk_rect detail_bounds;
    struct nk_command_buffer* canvas;
    struct nk_color marker;
    if (ui == NULL || tone > FOUNDATION_UI_LABEL_DANGER || !foundation_ui_string_valid(title) ||
        !foundation_ui_string_valid(detail) || title->length > INT32_MAX ||
        detail->length > INT32_MAX) {
        return;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return;
    foundation_ui_set_context_target(ui, bounds);
    marker = tone == FOUNDATION_UI_LABEL_DANGER ? nk_rgb(255, 112, 112) : ui->accent;
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    nk_stroke_rect(canvas, bounds, 6.0f, 1.0f, ui->context->style.window.border_color);
    nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 5.0f, 3.0f, bounds.h - 10.0f), 1.5f, marker);
    font = ui->context->style.font;
    title_bounds = nk_rect(bounds.x + 14.0f, bounds.y + 7.0f, bounds.w - 28.0f, font->height);
    nk_draw_text(canvas, title_bounds, foundation_ui_string_data(title), (int)title->length, font,
                 nk_rgba(0, 0, 0, 0), ui->text);
    if (detail->length != 0) {
        detail_bounds = nk_rect(bounds.x + 14.0f, bounds.y + bounds.h - font->height - 7.0f,
                                bounds.w - 28.0f, font->height);
        nk_draw_text(canvas, detail_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     font, nk_rgba(0, 0, 0, 0), ui->muted);
    }
}

bool foundation_ui_segment(uint64_t handle, const fdn_string* value, bool selected,
                           uint64_t position, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect background;
    struct nk_rect label;
    struct nk_command_buffer* canvas;
    struct nk_color fill;
    struct nk_color foreground;
    nk_flags state = 0;
    float text_width;
    float overlap;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX ||
        position > FOUNDATION_UI_SEGMENT_LAST) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    background = bounds;
    overlap = ui->context->style.window.spacing.x * 0.5f;
    if (position == FOUNDATION_UI_SEGMENT_FIRST || position == FOUNDATION_UI_SEGMENT_MIDDLE)
        background.w += overlap;
    if (position == FOUNDATION_UI_SEGMENT_MIDDLE || position == FOUNDATION_UI_SEGMENT_LAST) {
        background.x -= overlap;
        background.w += overlap;
    }
    fill = selected ? ui->accent : ui->raised;
    if ((state & NK_WIDGET_STATE_HOVER) != 0)
        fill = foundation_ui_shift_color(fill, 16);
    if (!enabled)
        fill.a = 110;
    foreground = selected ? nk_rgb(9, 12, 18) : ui->text;
    if (!enabled)
        foreground.a = 110;
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, background, 6.0f, fill);
    nk_stroke_rect(canvas, background, 6.0f, 1.0f,
                   selected ? ui->accent : ui->context->style.window.border_color);
    font = ui->context->style.font;
    text_width = font->width(font->userdata, font->height, foundation_ui_string_data(value),
                             (int)value->length);
    label = nk_rect(bounds.x + (bounds.w - text_width) * 0.5f,
                    bounds.y + (bounds.h - font->height) * 0.5f, text_width, font->height);
    nk_draw_text(canvas, label, foundation_ui_string_data(value), (int)value->length, font,
                 nk_rgba(0, 0, 0, 0), foreground);
    return pressed;
}

int32_t foundation_ui_slider(uint64_t handle, uint64_t value, uint64_t minimum, uint64_t maximum,
                             uint64_t step, uint64_t* result, bool* changed) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    int current;
    if (ui == NULL || result == NULL || changed == NULL || minimum >= maximum || minimum > value ||
        value > maximum || maximum > INT32_MAX || step == 0 || step > INT32_MAX) {
        return FOUNDATION_UI_INVALID;
    }
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    current = (int)value;
    *changed = nk_slider_int(ui->context, (int)minimum, &current, (int)maximum, (int)step) != 0;
    *result = (uint64_t)current;
    return FOUNDATION_UI_OK;
}

bool foundation_ui_file_entry(uint64_t handle, const fdn_string* name, const fdn_string* details,
                              bool directory) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect name_bounds;
    struct nk_rect details_bounds;
    struct nk_rect icon_bounds;
    struct nk_command_buffer* canvas;
    nk_flags state = 0;
    struct nk_color foreground;
    bool pressed;
    if (ui == NULL || !foundation_ui_string_valid(name) || !foundation_ui_string_valid(details) ||
        name->length > INT32_MAX || details->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if ((state & NK_WIDGET_STATE_HOVER) != 0) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    }
    foreground = directory ? ui->accent : ui->muted;
    icon_bounds = nk_rect(bounds.x + 10.0f, bounds.y + (bounds.h - 16.0f) * 0.5f, 18.0f, 16.0f);
    if (directory) {
        const float points[] = {icon_bounds.x,
                                icon_bounds.y + 3.0f,
                                icon_bounds.x + 7.0f,
                                icon_bounds.y + 3.0f,
                                icon_bounds.x + 10.0f,
                                icon_bounds.y + 6.0f,
                                icon_bounds.x + icon_bounds.w,
                                icon_bounds.y + 6.0f,
                                icon_bounds.x + icon_bounds.w,
                                icon_bounds.y + icon_bounds.h,
                                icon_bounds.x,
                                icon_bounds.y + icon_bounds.h,
                                icon_bounds.x,
                                icon_bounds.y + 3.0f};
        nk_stroke_polyline(canvas, points, 7, 1.5f, foreground);
    } else {
        nk_stroke_rect(canvas, icon_bounds, 2.0f, 1.5f, foreground);
        nk_stroke_line(canvas, icon_bounds.x + 5.0f, icon_bounds.y + 5.0f, icon_bounds.x + 13.0f,
                       icon_bounds.y + 5.0f, 1.5f, foreground);
        nk_stroke_line(canvas, icon_bounds.x + 5.0f, icon_bounds.y + 9.0f, icon_bounds.x + 13.0f,
                       icon_bounds.y + 9.0f, 1.5f, foreground);
    }
    name_bounds =
        nk_rect(bounds.x + 38.0f, bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
                bounds.w * 0.64f - 38.0f, ui->context->style.font->height);
    details_bounds = nk_rect(bounds.x + bounds.w * 0.64f,
                             bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
                             bounds.w * 0.34f, ui->context->style.font->height);
    nk_draw_text(canvas, name_bounds, foundation_ui_string_data(name), (int)name->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), ui->text);
    nk_draw_text(canvas, details_bounds, foundation_ui_string_data(details), (int)details->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), ui->muted);
    return pressed;
}

bool foundation_ui_navigation_item(uint64_t handle, const fdn_string* value, bool selected,
                                   bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect label;
    struct nk_command_buffer* canvas;
    struct nk_color foreground;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX)
        return false;
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if (selected) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
        nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 6.0f, 3.0f, bounds.h - 12.0f), 1.5f,
                     ui->accent);
    } else if ((state & NK_WIDGET_STATE_HOVER) != 0) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    }
    foreground = ui->text;
    if (!enabled)
        foreground.a = 110;
    label =
        nk_rect(bounds.x + 12.0f, bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
                bounds.w - 24.0f, ui->context->style.font->height);
    nk_draw_text(canvas, label, foundation_ui_string_data(value), (int)value->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), foreground);
    return pressed;
}

bool foundation_ui_sidebar_item(uint64_t handle, uint64_t icon, const fdn_string* value,
                                bool selected, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect icon_bounds;
    struct nk_rect label;
    struct nk_command_buffer* canvas;
    struct nk_color foreground;
    nk_flags state = 0;
    bool pressed = false;
    if (ui == NULL || icon > FOUNDATION_UI_ICON_ACCESSIBILITY ||
        !foundation_ui_string_valid(value) || value->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if (selected) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
        nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 6.0f, 3.0f, bounds.h - 12.0f), 1.5f,
                     ui->accent);
    } else if ((state & NK_WIDGET_STATE_HOVER) != 0) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    }
    foreground = selected ? ui->accent : ui->text;
    if (!enabled)
        foreground.a = 110;
    icon_bounds = nk_rect(bounds.x + 12.0f, bounds.y + (bounds.h - 20.0f) * 0.5f, 20.0f, 20.0f);
    foundation_ui_draw_icon(canvas, icon_bounds, icon, foreground);
    label =
        nk_rect(bounds.x + 42.0f, bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f,
                bounds.w - 54.0f, ui->context->style.font->height);
    nk_draw_text(canvas, label, foundation_ui_string_data(value), (int)value->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), foreground);
    return pressed;
}

bool foundation_ui_list_item(uint64_t handle, const fdn_string* title, const fdn_string* detail,
                             bool selected, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect title_bounds;
    struct nk_rect detail_bounds;
    struct nk_command_buffer* canvas;
    struct nk_color title_color;
    struct nk_color detail_color;
    nk_flags state = 0;
    float title_y;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(title) || !foundation_ui_string_valid(detail) ||
        title->length > INT32_MAX || detail->length > INT32_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    foundation_ui_set_context_target(ui, bounds);
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if (selected) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
        nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 5.0f, 3.0f, bounds.h - 10.0f), 1.5f,
                     ui->accent);
    } else if ((state & NK_WIDGET_STATE_HOVER) != 0) {
        nk_fill_rect(canvas, bounds, 6.0f, ui->raised);
    }
    title_color = ui->text;
    detail_color = ui->muted;
    if (!enabled) {
        title_color.a = 110;
        detail_color.a = 110;
    }
    title_y = detail->length == 0 ? bounds.y + (bounds.h - ui->context->style.font->height) * 0.5f
                                  : bounds.y + 6.0f;
    title_bounds =
        nk_rect(bounds.x + 12.0f, title_y, bounds.w - 24.0f, ui->context->style.font->height);
    nk_draw_text(canvas, title_bounds, foundation_ui_string_data(title), (int)title->length,
                 ui->context->style.font, nk_rgba(0, 0, 0, 0), title_color);
    if (detail->length != 0) {
        detail_bounds =
            nk_rect(bounds.x + 12.0f, bounds.y + bounds.h - ui->context->style.font->height - 6.0f,
                    bounds.w - 24.0f, ui->context->style.font->height);
        nk_draw_text(canvas, detail_bounds, foundation_ui_string_data(detail), (int)detail->length,
                     ui->context->style.font, nk_rgba(0, 0, 0, 0), detail_color);
    }
    return pressed;
}

bool foundation_ui_begin_picker(uint64_t handle, const fdn_string* value, float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    char* selected;
    bool opened;
    if (ui == NULL || ui->picker_active || !foundation_ui_string_valid(value) ||
        value->length > INT32_MAX || !isfinite(height) || height <= 0.0f) {
        return false;
    }
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    selected = foundation_ui_text(value);
    if (selected == NULL)
        return false;
    opened = nk_combo_begin_label(ui->context, selected, nk_vec2(bounds.w, height));
    SDL_free(selected);
    ui->picker_active = opened;
    return opened;
}

bool foundation_ui_begin_named_picker(uint64_t handle, const fdn_string* name,
                                      const fdn_string* value, float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_window* window;
    struct nk_rect bounds;
    nk_hash identity;
    unsigned int sequence;
    char* selected;
    bool opened;
    if (ui == NULL || ui->picker_active || !foundation_ui_string_valid(name) ||
        !foundation_ui_string_valid(value) || name->length == 0 || name->length > INT32_MAX ||
        value->length > INT32_MAX || !isfinite(height) || height <= 0.0f) {
        return false;
    }
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    selected = foundation_ui_text(value);
    if (selected == NULL)
        return false;
    window = ui->context->current;
    sequence = window->popup.combo_count;
    identity = nk_murmur_hash(foundation_ui_string_data(name), (int)name->length, NK_PANEL_COMBO);
    window->popup.combo_count = identity;
    opened = nk_combo_begin_label(ui->context, selected, nk_vec2(bounds.w, height));
    window->popup.combo_count = sequence + 1U;
    SDL_free(selected);
    ui->picker_active = opened;
    return opened;
}

bool foundation_ui_picker_item(uint64_t handle, const fdn_string* value, bool selected,
                               bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_style_button previous;
    char* label;
    bool pressed;
    if (ui == NULL || !ui->picker_active || !foundation_ui_string_valid(value) ||
        value->length > INT32_MAX) {
        return false;
    }
    label = foundation_ui_text(value);
    if (label == NULL)
        return false;
    nk_layout_row_dynamic(ui->context, 32.0f, 1);
    if (!enabled) {
        struct nk_color muted = ui->muted;
        muted.a = 110;
        nk_label_colored(ui->context, label, NK_TEXT_LEFT, muted);
        SDL_free(label);
        return false;
    }
    previous = ui->context->style.contextual_button;
    if (selected)
        ui->context->style.contextual_button.normal = nk_style_item_color(ui->raised);
    pressed = nk_combo_item_label(ui->context, label, NK_TEXT_LEFT);
    ui->context->style.contextual_button = previous;
    SDL_free(label);
    return pressed;
}

void foundation_ui_end_picker(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || !ui->picker_active)
        return;
    nk_combo_end(ui->context);
    ui->picker_active = false;
}

int32_t foundation_ui_progress(uint64_t handle, uint64_t value, uint64_t maximum) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    nk_size current;
    if (ui == NULL || maximum == 0 || value > maximum || maximum > SIZE_MAX)
        return FOUNDATION_UI_INVALID;
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
    current = (nk_size)value;
    (void)nk_progress(ui->context, &current, (nk_size)maximum, false);
    return FOUNDATION_UI_OK;
}

bool foundation_ui_begin_context_menu(uint64_t handle, float width, uint64_t items) {
    foundation_ui* ui = foundation_ui_from(handle);
    float height;
    if (ui == NULL || !ui->context_target_valid || ui->context_menu_active || !isfinite(width) ||
        width <= 0.0f || items == 0 || items > INT32_MAX) {
        return false;
    }
    height = 24.0f + 32.0f * (float)items;
    if (!isfinite(height) ||
        !nk_contextual_begin(ui->context, 0, nk_vec2(width, height), ui->context_target)) {
        return false;
    }
    ui->context_menu_active = true;
    ui->tooltip_length = 0;
    nk_layout_row_dynamic(ui->context, 32.0f, 1);
    return true;
}

bool foundation_ui_begin_popover(uint64_t handle, float width, float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    static const char name[] = "foundation-ui-popover";
    nk_flags state = 0;
    bool clicked;
    if (ui == NULL || !ui->context_target_valid || ui->context_menu_active || ui->popover_active ||
        !isfinite(width) || !isfinite(height) || width <= 0.0f || height <= 0.0f) {
        return false;
    }
    clicked = foundation_ui_button_input(&state, ui->context_target, &ui->context->input);
    if (!foundation_ui_popover_begin(ui, name, (int)(sizeof(name) - 1), width, height, clicked)) {
        return false;
    }
    ui->popover_active = true;
    ui->tooltip_length = 0;
    return true;
}

bool foundation_ui_begin_named_popover(uint64_t handle, const fdn_string* name, float width,
                                       float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    nk_flags state = 0;
    bool clicked;
    if (ui == NULL || !foundation_ui_string_valid(name) || name->length == 0 ||
        name->length > INT32_MAX || !ui->context_target_valid || ui->context_menu_active ||
        ui->popover_active || !isfinite(width) || !isfinite(height) || width <= 0.0f ||
        height <= 0.0f) {
        return false;
    }
    clicked = foundation_ui_button_input(&state, ui->context_target, &ui->context->input);
    if (!foundation_ui_popover_begin(ui, foundation_ui_string_data(name), (int)name->length, width,
                                     height, clicked)) {
        return false;
    }
    ui->popover_active = true;
    ui->tooltip_length = 0;
    return true;
}

bool foundation_ui_begin_toolbar_popover(uint64_t handle, const fdn_string* name, uint64_t icon,
                                         const fdn_string* label, float width, float height,
                                         bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    bool clicked;
    if (ui == NULL || !foundation_ui_string_valid(name) || name->length == 0 ||
        name->length > INT32_MAX || !foundation_ui_string_valid(label) ||
        icon > FOUNDATION_UI_ICON_ACCESSIBILITY || ui->context_menu_active || ui->popover_active ||
        !isfinite(width) || !isfinite(height) || width <= 0.0f || height <= 0.0f) {
        return false;
    }
    clicked = foundation_ui_compact_icon_button(handle, icon, label, false, enabled);
    if (!enabled || !foundation_ui_popover_begin(ui, foundation_ui_string_data(name),
                                                 (int)name->length, width, height, clicked)) {
        return false;
    }
    ui->popover_active = true;
    ui->tooltip_length = 0;
    return true;
}

void foundation_ui_close_popover(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_window* popup;
    struct nk_panel* panel;
    if (ui == NULL || !ui->popover_active)
        return;
    popup = ui->context->current;
    while (popup != NULL) {
        panel = popup->layout;
        while (panel != NULL && ((int)panel->type & (int)NK_PANEL_SET_POPUP) == 0)
            panel = panel->parent;
        if (panel != NULL)
            break;
        popup = popup->parent;
    }
    if (popup == NULL)
        return;
    popup->flags |= NK_WINDOW_HIDDEN;
    ui->popover_visible = false;
}

void foundation_ui_end_popover(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || !ui->popover_active) {
        return;
    }
    nk_popup_end(ui->context);
    ui->popover_active = false;
}

bool foundation_ui_context_menu_item(uint64_t handle, const fdn_string* label, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    bool selected;
    if (ui == NULL || !ui->context_menu_active || !foundation_ui_string_valid(label) ||
        label->length > INT32_MAX) {
        return false;
    }
    if (!enabled) {
        nk_widget_disable_begin(ui->context);
        ui->context->style.contextual_button.color_factor_background = 1.0f;
    }
    selected = nk_contextual_item_text(ui->context, foundation_ui_string_data(label),
                                       (int)label->length, NK_TEXT_LEFT);
    if (!enabled)
        nk_widget_disable_end(ui->context);
    return enabled && selected;
}

void foundation_ui_end_context_menu(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || !ui->context_menu_active)
        return;
    nk_contextual_end(ui->context);
    ui->context_menu_active = false;
}
