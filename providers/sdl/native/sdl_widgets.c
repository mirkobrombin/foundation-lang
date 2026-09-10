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
    struct nk_color previous;
    struct nk_color selected;
    if (ui == NULL || !foundation_ui_string_valid(value) || value->length > INT32_MAX)
        return;
    bounds = nk_widget_bounds(ui->context);
    foundation_ui_set_context_target(ui, bounds);
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
        nk_text_wrap(ui->context, foundation_ui_string_data(value), (int)value->length);
    } else {
        nk_text(ui->context, foundation_ui_string_data(value), (int)value->length, NK_TEXT_LEFT);
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

bool foundation_ui_switch(uint64_t handle, bool value, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_rect track;
    struct nk_rect knob;
    struct nk_command_buffer* canvas;
    struct nk_color track_color;
    struct nk_color knob_color;
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
    return pressed;
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
        value > maximum ||
        maximum > INT32_MAX || step == 0 || step > INT32_MAX) {
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
    nk_layout_row_dynamic(ui->context, 32.0f, 1);
    return true;
}

bool foundation_ui_begin_popover(uint64_t handle, float width, float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui == NULL || !ui->context_target_valid || ui->context_menu_active ||
        ui->popover_active || !isfinite(width) || !isfinite(height) || width <= 0.0f ||
        height <= 0.0f) {
        return false;
    }
    if (!foundation_ui_popover_begin(ui, width, height)) {
        return false;
    }
    ui->popover_active = true;
    return true;
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
