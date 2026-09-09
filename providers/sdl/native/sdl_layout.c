#include "sdl_ui_internal.h"

void foundation_ui_row(uint64_t handle, float height, uint64_t columns) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL && isfinite(height) && height > 0.0f && columns > 0 && columns <= INT32_MAX) {
        nk_layout_row_dynamic(ui->context, height, (int)columns);
    }
}

void foundation_ui_row_begin(uint64_t handle, float height, uint64_t columns) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL && isfinite(height) && height > 0.0f && columns > 0 && columns <= INT32_MAX) {
        nk_layout_row_begin(ui->context, NK_DYNAMIC, height, (int)columns);
    }
}

void foundation_ui_row_push(uint64_t handle, float ratio) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL && isfinite(ratio) && ratio > 0.0f)
        nk_layout_row_push(ui->context, ratio);
}

void foundation_ui_row_end(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL)
        nk_layout_row_end(ui->context);
}

static bool foundation_ui_begin_group_style(uint64_t handle, const fdn_string* name,
                                            bool scrollable, bool compact) {
    foundation_ui* ui = foundation_ui_from(handle);
    char* group_name;
    struct nk_vec2 previous_padding;
    struct nk_vec2 previous_spacing;
    struct nk_vec2 previous_scrollbar_size;
    bool visible;
    if (ui == NULL || ui->group_depth == FOUNDATION_UI_GROUP_CAPACITY)
        return false;
    group_name = foundation_ui_text(name);
    if (group_name == NULL)
        return false;
    previous_padding = ui->context->style.window.group_padding;
    previous_spacing = ui->context->style.window.spacing;
    previous_scrollbar_size = ui->context->style.window.scrollbar_size;
    if (compact) {
        ui->context->style.window.group_padding = nk_vec2(4.0f, 0.0f);
        ui->context->style.window.spacing = nk_vec2(0.0f, 2.0f);
    } else if (scrollable) {
        ui->context->style.window.group_padding = nk_vec2(8.0f, 4.0f);
        ui->context->style.window.spacing = nk_vec2(8.0f, 2.0f);
    }
    if (compact)
        ui->context->style.window.scrollbar_size = nk_vec2(0.0f, 0.0f);
    visible = nk_group_begin(ui->context, group_name, scrollable ? 0 : NK_WINDOW_NO_SCROLLBAR);
    ui->context->style.window.group_padding = previous_padding;
    if (!visible) {
        ui->context->style.window.spacing = previous_spacing;
        ui->context->style.window.scrollbar_size = previous_scrollbar_size;
    }
    if (visible) {
        foundation_ui_group_state* state = &ui->group_states[ui->group_depth++];
        state->spacing = previous_spacing;
        state->scrollbar_size = previous_scrollbar_size;
        state->compact = compact;
    }
    SDL_free(group_name);
    return visible;
}

bool foundation_ui_begin_group(uint64_t handle, const fdn_string* name, bool scrollable) {
    return foundation_ui_begin_group_style(handle, name, scrollable, false);
}

bool foundation_ui_begin_compact_group(uint64_t handle, const fdn_string* name) {
    return foundation_ui_begin_group_style(handle, name, true, true);
}

void foundation_ui_end_group(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    foundation_ui_group_state* state;
    if (ui == NULL || ui->group_depth == 0)
        return;
    nk_group_end(ui->context);
    state = &ui->group_states[--ui->group_depth];
    ui->context->style.window.spacing = state->spacing;
    if (state->compact)
        ui->context->style.window.scrollbar_size = state->scrollbar_size;
}

void foundation_ui_space(uint64_t handle, float height) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL && isfinite(height) && height > 0.0f) {
        nk_layout_row_dynamic(ui->context, height, 1);
        nk_spacing(ui->context, 1);
    }
}

void foundation_ui_empty(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    if (ui != NULL)
        nk_spacing(ui->context, 1);
}

void foundation_ui_separator(uint64_t handle) {
    foundation_ui* ui = foundation_ui_from(handle);
    struct nk_rect bounds;
    struct nk_command_buffer* canvas;
    if (ui == NULL)
        return;
    nk_layout_row_dynamic(ui->context, 1.0f, 1);
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID)
        return;
    canvas = nk_window_get_canvas(ui->context);
    nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f,
                   ui->context->style.window.border_color);
}
