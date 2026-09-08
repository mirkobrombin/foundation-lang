#include "sdl_ui_internal.h"

static void foundation_ui_control_tooltip(foundation_ui* ui, const fdn_string* label,
                                          struct nk_rect bounds, nk_flags state) {
    if ((state & NK_WIDGET_STATE_HOVER) == 0)
        return;
    ui->tooltip_length =
        label->length < sizeof(ui->tooltip) ? label->length : sizeof(ui->tooltip) - 1;
    if (ui->tooltip_length != 0)
        SDL_memcpy(ui->tooltip, label->data, (size_t)ui->tooltip_length);
    ui->tooltip[ui->tooltip_length] = '\0';
    ui->tooltip_anchor = bounds;
}

bool foundation_ui_icon_button(uint64_t handle, uint64_t icon, const fdn_string* label,
                               bool selected, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_command_buffer* canvas;
    nk_flags state = 0;
    struct nk_color foreground;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(label) || icon > FOUNDATION_UI_ICON_WEB)
        return false;
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if (selected) {
        nk_fill_rect(canvas, bounds, 8.0f, ui->accent);
        foreground = nk_rgb(9, 12, 18);
    } else if ((state & NK_WIDGET_STATE_HOVER) != 0) {
        nk_fill_rect(canvas, bounds, 8.0f, ui->raised);
        foreground = ui->text;
    } else {
        foreground = enabled ? ui->muted : nk_rgba(91, 105, 125, 110);
    }
    foundation_ui_draw_icon(canvas, bounds, icon, foreground);
    foundation_ui_control_tooltip(ui, label, bounds, state);
    return pressed;
}

bool foundation_ui_monogram_button(uint64_t handle, const fdn_string* monogram,
                                   const fdn_string* label, uint64_t red, uint64_t green,
                                   uint64_t blue, uint64_t alpha, bool selected, bool enabled) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_rect bounds;
    struct nk_rect badge;
    struct nk_command_buffer* canvas;
    struct nk_color color;
    nk_flags state = 0;
    float width;
    bool pressed = false;
    if (ui == NULL || !foundation_ui_string_valid(monogram) || monogram->length == 0 ||
        monogram->length > 8 || !foundation_ui_string_valid(label) || red > UINT8_MAX ||
        green > UINT8_MAX || blue > UINT8_MAX || alpha > UINT8_MAX) {
        return false;
    }
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return false;
    if (enabled)
        pressed = foundation_ui_button_input(&state, bounds, &ui->context->input);
    canvas = nk_window_get_canvas(ui->context);
    if (selected || (state & NK_WIDGET_STATE_HOVER) != 0)
        nk_fill_rect(canvas, bounds, 8.0f, ui->raised);
    color = nk_rgba((nk_byte)red, (nk_byte)green, (nk_byte)blue,
                    enabled ? (nk_byte)alpha : (nk_byte)(alpha / 2));
    badge = bounds;
    badge.x += (bounds.w - 34.0f) * 0.5f;
    badge.y += (bounds.h - 34.0f) * 0.5f;
    badge.w = 34.0f;
    badge.h = 34.0f;
    nk_fill_rect(canvas, badge, 10.0f, color);
    font = &ui->regular_font->handle;
    width = font->width(font->userdata, font->height, foundation_ui_string_data(monogram),
                        (int)monogram->length);
    nk_draw_text(canvas,
                 nk_rect(badge.x + (badge.w - width) * 0.5f,
                         badge.y + (badge.h - font->height) * 0.5f, width, font->height),
                 foundation_ui_string_data(monogram), (int)monogram->length, font,
                 nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
    foundation_ui_control_tooltip(ui, label, bounds, state);
    return pressed;
}
