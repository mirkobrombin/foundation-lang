#include "sdl_ui_internal.h"

static void foundation_ui_draw_theme(struct nk_command_buffer* canvas, float x, float y,
                                     struct nk_color color) {
    nk_stroke_circle(canvas, nk_rect(x - 6.0f, y - 6.0f, 12.0f, 12.0f), 2.0f, color);
    nk_stroke_line(canvas, x, y - 10.0f, x, y - 7.0f, 2.0f, color);
    nk_stroke_line(canvas, x, y + 7.0f, x, y + 10.0f, 2.0f, color);
    nk_stroke_line(canvas, x - 10.0f, y, x - 7.0f, y, 2.0f, color);
    nk_stroke_line(canvas, x + 7.0f, y, x + 10.0f, y, 2.0f, color);
}

static void foundation_ui_draw_arrow(struct nk_command_buffer* canvas, float x, float y,
                                     float direction, struct nk_color color) {
    nk_stroke_line(canvas, x - 7.0f * direction, y, x + 7.0f * direction, y, 2.0f, color);
    nk_stroke_line(canvas, x - 7.0f * direction, y, x - 2.0f * direction, y - 5.0f, 2.0f, color);
    nk_stroke_line(canvas, x - 7.0f * direction, y, x - 2.0f * direction, y + 5.0f, 2.0f, color);
}

void foundation_ui_draw_icon(struct nk_command_buffer* canvas, struct nk_rect bounds, uint64_t icon,
                             struct nk_color color) {
    const float x = bounds.x + bounds.w * 0.5f;
    const float y = bounds.y + bounds.h * 0.5f;
    const float left = x - 9.0f;
    const float right = x + 9.0f;
    const float top = y - 8.0f;
    const float bottom = y + 8.0f;
    if (icon == FOUNDATION_UI_ICON_LINK) {
        nk_stroke_circle(canvas, nk_rect(left, y - 4.0f, 9.0f, 9.0f), 2.0f, color);
        nk_stroke_circle(canvas, nk_rect(x, y - 4.0f, 9.0f, 9.0f), 2.0f, color);
        nk_stroke_line(canvas, x - 4.0f, y, x + 4.0f, y, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_SHARE || icon == FOUNDATION_UI_ICON_DOWNLOAD) {
        const float direction = icon == FOUNDATION_UI_ICON_SHARE ? -1.0f : 1.0f;
        nk_stroke_rect(canvas, nk_rect(left, y, 18.0f, 10.0f), 2.0f, 2.0f, color);
        nk_stroke_line(canvas, x, y - 7.0f * direction, x, y + 3.0f * direction, 2.0f, color);
        nk_stroke_line(canvas, x, y - 7.0f * direction, x - 5.0f, y - 2.0f * direction, 2.0f,
                       color);
        nk_stroke_line(canvas, x, y - 7.0f * direction, x + 5.0f, y - 2.0f * direction, 2.0f,
                       color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_TERMINAL) {
        nk_stroke_rect(canvas, nk_rect(left, top, 18.0f, 16.0f), 3.0f, 2.0f, color);
        nk_stroke_line(canvas, left + 4.0f, y - 3.0f, left + 8.0f, y, 2.0f, color);
        nk_stroke_line(canvas, left + 8.0f, y, left + 4.0f, y + 3.0f, 2.0f, color);
        nk_stroke_line(canvas, x + 1.0f, y + 4.0f, right - 3.0f, y + 4.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_FOLDER) {
        const float points[] = {left,       top + 3.0f, x - 2.0f,   top + 3.0f, x + 1.0f,
                                top + 6.0f, right,      top + 6.0f, right,      bottom,
                                left,       bottom,     left,       top + 3.0f};
        nk_stroke_polyline(canvas, points, 7, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_DISPLAY) {
        nk_stroke_rect(canvas, nk_rect(left, top, 18.0f, 13.0f), 2.0f, 2.0f, color);
        nk_stroke_line(canvas, x, y + 5.0f, x, bottom, 2.0f, color);
        nk_stroke_line(canvas, x - 6.0f, bottom, x + 6.0f, bottom, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_THEME || icon == FOUNDATION_UI_ICON_SETTINGS) {
        foundation_ui_draw_theme(canvas, x, y, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_HOME) {
        const float roof[] = {left, y - 1.0f, x, top, right, y - 1.0f};
        nk_stroke_polyline(canvas, roof, 3, 2.0f, color);
        nk_stroke_rect(canvas, nk_rect(left + 3.0f, y - 1.0f, 12.0f, 9.0f), 1.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_ADD) {
        nk_stroke_line(canvas, x - 7.0f, y, x + 7.0f, y, 2.0f, color);
        nk_stroke_line(canvas, x, y - 7.0f, x, y + 7.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_WORKSPACE) {
        nk_stroke_rect(canvas, nk_rect(left, top, 7.0f, 7.0f), 1.0f, 1.5f, color);
        nk_stroke_rect(canvas, nk_rect(x + 2.0f, top, 7.0f, 7.0f), 1.0f, 1.5f, color);
        nk_stroke_rect(canvas, nk_rect(left, y + 2.0f, 7.0f, 7.0f), 1.0f, 1.5f, color);
        nk_stroke_rect(canvas, nk_rect(x + 2.0f, y + 2.0f, 7.0f, 7.0f), 1.0f, 1.5f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_KEY) {
        nk_stroke_circle(canvas, nk_rect(left, y - 5.0f, 10.0f, 10.0f), 2.0f, color);
        nk_stroke_line(canvas, x - 1.0f, y, right, y, 2.0f, color);
        nk_stroke_line(canvas, x + 5.0f, y, x + 5.0f, y + 4.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_BACK || icon == FOUNDATION_UI_ICON_FORWARD) {
        foundation_ui_draw_arrow(canvas, x, y, icon == FOUNDATION_UI_ICON_BACK ? 1.0f : -1.0f,
                                 color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_RELOAD) {
        nk_stroke_circle(canvas, nk_rect(x - 7.0f, y - 7.0f, 14.0f, 14.0f), 2.0f, color);
        nk_stroke_line(canvas, x + 2.0f, y - 7.0f, x + 8.0f, y - 7.0f, 2.0f, color);
        nk_stroke_line(canvas, x + 8.0f, y - 7.0f, x + 8.0f, y - 1.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_EXTERNAL) {
        nk_stroke_rect(canvas, nk_rect(left, y - 5.0f, 13.0f, 13.0f), 1.0f, 2.0f, color);
        nk_stroke_line(canvas, x - 1.0f, y + 1.0f, right, top, 2.0f, color);
        nk_stroke_line(canvas, right - 6.0f, top, right, top, 2.0f, color);
        nk_stroke_line(canvas, right, top, right, top + 6.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_SPLIT) {
        nk_stroke_rect(canvas, nk_rect(left, top, 18.0f, 16.0f), 2.0f, 2.0f, color);
        nk_stroke_line(canvas, x, top, x, bottom, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_NOTIFICATION) {
        const float bell[] = {left + 3.0f,  y + 4.0f, left + 5.0f,  y + 1.0f,     left + 5.0f,
                              y - 3.0f,     x,        top,          right - 5.0f, y - 3.0f,
                              right - 5.0f, y + 1.0f, right - 3.0f, y + 4.0f};
        nk_stroke_polyline(canvas, bell, 7, 2.0f, color);
        nk_stroke_line(canvas, left + 2.0f, y + 4.0f, right - 2.0f, y + 4.0f, 2.0f, color);
        nk_stroke_circle(canvas, nk_rect(x - 1.5f, y + 6.0f, 3.0f, 3.0f), 1.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_WEB) {
        nk_stroke_circle(canvas, nk_rect(left, top, 18.0f, 16.0f), 2.0f, color);
        nk_stroke_line(canvas, left + 1.0f, y, right - 1.0f, y, 1.5f, color);
        nk_stroke_line(canvas, x, top + 1.0f, x, bottom - 1.0f, 1.5f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_SHIELD) {
        const float shield[] = {x, top,    right - 1.0f, top + 3.0f, right - 2.0f, y + 5.0f,
                                x, bottom, left + 2.0f,  y + 5.0f,   left + 1.0f,  top + 3.0f,
                                x, top};
        nk_stroke_polyline(canvas, shield, 7, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_EXTENSION) {
        nk_stroke_rect(canvas, nk_rect(left + 2.0f, top + 2.0f, 14.0f, 13.0f), 2.0f, 2.0f, color);
        nk_stroke_circle(canvas, nk_rect(x - 3.0f, top - 1.0f, 6.0f, 6.0f), 1.5f, color);
        nk_stroke_circle(canvas, nk_rect(right - 3.0f, y - 3.0f, 6.0f, 6.0f), 1.5f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_INFORMATION) {
        nk_stroke_circle(canvas, nk_rect(left + 1.0f, top, 16.0f, 16.0f), 2.0f, color);
        nk_fill_circle(canvas, nk_rect(x - 1.0f, top + 3.0f, 2.0f, 2.0f), color);
        nk_stroke_line(canvas, x, y - 1.0f, x, bottom - 3.0f, 2.0f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_STORAGE) {
        nk_stroke_rect(canvas, nk_rect(left, top + 2.0f, 18.0f, 13.0f), 2.0f, 2.0f, color);
        nk_stroke_line(canvas, left + 3.0f, y + 3.0f, right - 3.0f, y + 3.0f, 1.5f, color);
        nk_fill_circle(canvas, nk_rect(right - 5.0f, top + 5.0f, 2.0f, 2.0f), color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_ACCESSIBILITY) {
        nk_stroke_circle(canvas, nk_rect(x - 2.5f, top, 5.0f, 5.0f), 1.5f, color);
        nk_stroke_line(canvas, left + 2.0f, y - 2.0f, right - 2.0f, y - 2.0f, 2.0f, color);
        nk_stroke_line(canvas, x, y - 2.0f, x, y + 4.0f, 2.0f, color);
        nk_stroke_line(canvas, x, y + 3.0f, left + 3.0f, bottom, 2.0f, color);
        nk_stroke_line(canvas, x, y + 3.0f, right - 3.0f, bottom, 2.0f, color);
        return;
    }
}

void foundation_ui_draw_compact_icon(struct nk_command_buffer* canvas, struct nk_rect bounds,
                                     uint64_t icon, struct nk_color color) {
    const float x = bounds.x + bounds.w * 0.5f;
    const float y = bounds.y + bounds.h * 0.5f;
    const float left = x - 6.0f;
    const float right = x + 6.0f;
    const float top = y - 6.0f;
    const float bottom = y + 6.0f;
    if (icon == FOUNDATION_UI_ICON_BACK || icon == FOUNDATION_UI_ICON_FORWARD) {
        const float direction = icon == FOUNDATION_UI_ICON_BACK ? 1.0f : -1.0f;
        nk_stroke_line(canvas, x - 5.0f * direction, y, x + 5.0f * direction, y, 1.25f, color);
        nk_stroke_line(canvas, x - 5.0f * direction, y, x - 1.5f * direction, y - 3.5f, 1.25f,
                       color);
        nk_stroke_line(canvas, x - 5.0f * direction, y, x - 1.5f * direction, y + 3.5f, 1.25f,
                       color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_RELOAD) {
        nk_stroke_circle(canvas, nk_rect(x - 5.0f, y - 5.0f, 10.0f, 10.0f), 1.25f, color);
        nk_stroke_line(canvas, x + 1.0f, y - 5.0f, x + 6.0f, y - 5.0f, 1.25f, color);
        nk_stroke_line(canvas, x + 6.0f, y - 5.0f, x + 6.0f, y, 1.25f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_EXTERNAL) {
        nk_stroke_rect(canvas, nk_rect(left, y - 4.0f, 9.0f, 9.0f), 1.0f, 1.25f, color);
        nk_stroke_line(canvas, x - 1.0f, y + 1.0f, right, top, 1.25f, color);
        nk_stroke_line(canvas, right - 4.0f, top, right, top, 1.25f, color);
        nk_stroke_line(canvas, right, top, right, top + 4.0f, 1.25f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_SPLIT) {
        nk_stroke_rect(canvas, nk_rect(left, top, 12.0f, 12.0f), 1.0f, 1.25f, color);
        nk_stroke_line(canvas, x, top, x, bottom, 1.25f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_NOTIFICATION) {
        const float bell[] = {left + 2.0f,  y + 3.0f, left + 3.0f,  y + 1.0f,     left + 3.0f,
                              y - 2.0f,     x,        top,          right - 3.0f, y - 2.0f,
                              right - 3.0f, y + 1.0f, right - 2.0f, y + 3.0f};
        nk_stroke_polyline(canvas, bell, 7, 1.25f, color);
        nk_stroke_line(canvas, left + 1.0f, y + 3.0f, right - 1.0f, y + 3.0f, 1.25f, color);
        return;
    }
    if (icon == FOUNDATION_UI_ICON_DOWNLOAD || icon == FOUNDATION_UI_ICON_SHARE) {
        const float direction = icon == FOUNDATION_UI_ICON_SHARE ? -1.0f : 1.0f;
        nk_stroke_rect(canvas, nk_rect(left, y, 12.0f, 6.0f), 1.0f, 1.25f, color);
        nk_stroke_line(canvas, x, y - 5.0f * direction, x, y + 2.0f * direction, 1.25f, color);
        nk_stroke_line(canvas, x, y - 5.0f * direction, x - 3.5f, y - 1.5f * direction, 1.25f,
                       color);
        nk_stroke_line(canvas, x, y - 5.0f * direction, x + 3.5f, y - 1.5f * direction, 1.25f,
                       color);
        return;
    }
    foundation_ui_draw_icon(canvas, bounds, icon, color);
}
