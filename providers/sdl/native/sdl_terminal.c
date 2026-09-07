#include "sdl_ui_internal.h"

static bool foundation_ui_terminal_append(foundation_ui* ui, const char* value, size_t length) {
    if (length > FOUNDATION_UI_TERMINAL_INPUT_CAPACITY - ui->terminal_input_length) {
        ui->event_overflow = true;
        return false;
    }
    if (length != 0) {
        SDL_memcpy(ui->terminal_input + ui->terminal_input_length, value, length);
        ui->terminal_input_length += length;
    }
    return true;
}

static void foundation_ui_terminal_paste(foundation_ui* ui) {
    char* value;
    if (!SDL_HasClipboardText())
        return;
    value = SDL_GetClipboardText();
    if (value == NULL)
        return;
    (void)foundation_ui_terminal_append(ui, value, SDL_strlen(value));
    SDL_free(value);
}

static const char* foundation_ui_terminal_key(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_UP:
        return "\x1b[A";
    case SDL_SCANCODE_DOWN:
        return "\x1b[B";
    case SDL_SCANCODE_RIGHT:
        return "\x1b[C";
    case SDL_SCANCODE_LEFT:
        return "\x1b[D";
    case SDL_SCANCODE_HOME:
        return "\x1b[H";
    case SDL_SCANCODE_END:
        return "\x1b[F";
    case SDL_SCANCODE_INSERT:
        return "\x1b[2~";
    case SDL_SCANCODE_DELETE:
        return "\x1b[3~";
    case SDL_SCANCODE_PAGEUP:
        return "\x1b[5~";
    case SDL_SCANCODE_PAGEDOWN:
        return "\x1b[6~";
    case SDL_SCANCODE_F1:
        return "\x1bOP";
    case SDL_SCANCODE_F2:
        return "\x1bOQ";
    case SDL_SCANCODE_F3:
        return "\x1bOR";
    case SDL_SCANCODE_F4:
        return "\x1bOS";
    case SDL_SCANCODE_F5:
        return "\x1b[15~";
    case SDL_SCANCODE_F6:
        return "\x1b[17~";
    case SDL_SCANCODE_F7:
        return "\x1b[18~";
    case SDL_SCANCODE_F8:
        return "\x1b[19~";
    case SDL_SCANCODE_F9:
        return "\x1b[20~";
    case SDL_SCANCODE_F10:
        return "\x1b[21~";
    case SDL_SCANCODE_F11:
        return "\x1b[23~";
    case SDL_SCANCODE_F12:
        return "\x1b[24~";
    default:
        return NULL;
    }
}

static uint8_t foundation_ui_terminal_control(SDL_Scancode scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return (uint8_t)(scancode - SDL_SCANCODE_A + 1);
    }
    if (scancode == SDL_SCANCODE_LEFTBRACKET)
        return 27;
    if (scancode == SDL_SCANCODE_BACKSLASH)
        return 28;
    if (scancode == SDL_SCANCODE_RIGHTBRACKET)
        return 29;
    if (scancode == SDL_SCANCODE_6)
        return 30;
    if (scancode == SDL_SCANCODE_MINUS)
        return 31;
    return 0;
}

bool foundation_ui_handle_terminal_event(foundation_ui* ui, const SDL_Event* event) {
    const char* sequence;
    size_t history;
    size_t amount;
    uint8_t control;
    char byte;
    if (event->type == SDL_EVENT_MOUSE_WHEEL && ui->terminal_bounds_valid &&
        event->wheel.mouse_x >= ui->terminal_bounds.x &&
        event->wheel.mouse_y >= ui->terminal_bounds.y &&
        event->wheel.mouse_x < ui->terminal_bounds.x + ui->terminal_bounds.w &&
        event->wheel.mouse_y < ui->terminal_bounds.y + ui->terminal_bounds.h) {
        history = foundation_terminal_history_rows(ui->terminal);
        amount = event->wheel.y < 0.0f ? 3 : 0;
        if (event->wheel.y > 0.0f) {
            ui->terminal_scroll =
                ui->terminal_scroll + 3 > history ? history : ui->terminal_scroll + 3;
        } else if (amount >= ui->terminal_scroll) {
            ui->terminal_scroll = 0;
        } else {
            ui->terminal_scroll -= amount;
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (foundation_ui_surface_at(ui, event->button.x, event->button.y) == NULL &&
            ui->terminal_bounds_valid && event->button.x >= ui->terminal_bounds.x &&
            event->button.y >= ui->terminal_bounds.y &&
            event->button.x < ui->terminal_bounds.x + ui->terminal_bounds.w &&
            event->button.y < ui->terminal_bounds.y + ui->terminal_bounds.h) {
            ui->terminal_focus = true;
            (void)SDL_StartTextInput(ui->window);
            if (event->button.button == SDL_BUTTON_RIGHT) {
                foundation_ui_terminal_paste(ui);
            }
            return true;
        }
        if (ui->terminal_focus) {
            ui->terminal_focus = false;
            (void)SDL_StopTextInput(ui->window);
        }
    }
    if (!ui->terminal_focus)
        return false;
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
            (void)foundation_ui_terminal_append(ui, "\x1b", 1);
        }
        (void)foundation_ui_terminal_append(ui, event->text.text, SDL_strlen(event->text.text));
        ui->terminal_scroll = 0;
        return true;
    }
    if (event->type != SDL_EVENT_KEY_DOWN) {
        return event->type == SDL_EVENT_KEY_UP;
    }
    if ((event->key.mod & SDL_KMOD_CTRL) != 0 && (event->key.mod & SDL_KMOD_SHIFT) != 0 &&
        event->key.scancode == SDL_SCANCODE_V) {
        foundation_ui_terminal_paste(ui);
        return true;
    }
    history = foundation_terminal_history_rows(ui->terminal);
    if ((event->key.mod & SDL_KMOD_SHIFT) != 0 && event->key.scancode == SDL_SCANCODE_PAGEUP) {
        amount = foundation_terminal_rows(ui->terminal) / 2;
        ui->terminal_scroll =
            ui->terminal_scroll + amount > history ? history : ui->terminal_scroll + amount;
        return true;
    }
    if ((event->key.mod & SDL_KMOD_SHIFT) != 0 && event->key.scancode == SDL_SCANCODE_PAGEDOWN) {
        amount = foundation_terminal_rows(ui->terminal) / 2;
        ui->terminal_scroll = amount >= ui->terminal_scroll ? 0 : ui->terminal_scroll - amount;
        return true;
    }
    if ((event->key.mod & SDL_KMOD_SHIFT) != 0 && event->key.scancode == SDL_SCANCODE_INSERT) {
        foundation_ui_terminal_paste(ui);
        return true;
    }
    if ((event->key.mod & SDL_KMOD_CTRL) != 0) {
        control = foundation_ui_terminal_control(event->key.scancode);
        if (control != 0) {
            byte = (char)control;
            (void)foundation_ui_terminal_append(ui, &byte, 1);
        }
        ui->terminal_scroll = 0;
        return true;
    }
    if (event->key.scancode == SDL_SCANCODE_RETURN ||
        event->key.scancode == SDL_SCANCODE_KP_ENTER) {
        ui->terminal_scroll = 0;
        return foundation_ui_terminal_append(ui, "\r", 1);
    }
    if (event->key.scancode == SDL_SCANCODE_BACKSPACE) {
        ui->terminal_scroll = 0;
        return foundation_ui_terminal_append(ui, "\x7f", 1);
    }
    if (event->key.scancode == SDL_SCANCODE_TAB) {
        return foundation_ui_terminal_append(ui, "\t", 1);
    }
    if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return foundation_ui_terminal_append(ui, "\x1b", 1);
    }
    sequence = foundation_ui_terminal_key(event->key.scancode);
    if (sequence != NULL) {
        ui->terminal_scroll = 0;
        (void)foundation_ui_terminal_append(ui, sequence, SDL_strlen(sequence));
        return true;
    }
    return true;
}

static struct nk_color foundation_ui_terminal_color(foundation_ui* ui, uint16_t value,
                                                    bool foreground) {
    static const uint8_t base[16][3] = {
        {15, 24, 40},    {205, 73, 73},   {95, 221, 122},  {229, 192, 82},
        {82, 139, 214},  {179, 101, 201}, {77, 184, 195},  {210, 219, 232},
        {92, 106, 126},  {255, 112, 112}, {111, 235, 138}, {244, 211, 106},
        {112, 165, 232}, {206, 130, 226}, {104, 211, 222}, {247, 249, 252},
    };
    uint16_t selected = value;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if (value == FOUNDATION_TERMINAL_DEFAULT_COLOR) {
        return foreground ? ui->text : ui->background;
    }
    if (selected < 16)
        return nk_rgb(base[selected][0], base[selected][1], base[selected][2]);
    if (selected >= 232) {
        red = (uint8_t)(8 + (selected - 232) * 10);
        return nk_rgb(red, red, red);
    }
    selected = (uint16_t)(selected - 16);
    red = (uint8_t)((selected / 36) == 0 ? 0 : 55 + (selected / 36) * 40);
    green = (uint8_t)(((selected / 6) % 6) == 0 ? 0 : 55 + ((selected / 6) % 6) * 40);
    blue = (uint8_t)((selected % 6) == 0 ? 0 : 55 + (selected % 6) * 40);
    return nk_rgb(red, green, blue);
}

static size_t foundation_ui_terminal_rune(char* output, uint32_t rune) {
    if (rune <= 0x7f) {
        output[0] = (char)rune;
        return 1;
    }
    if (rune <= 0x7ff) {
        output[0] = (char)(0xc0U | (rune >> 6));
        output[1] = (char)(0x80U | (rune & 0x3fU));
        return 2;
    }
    if (rune <= 0xffff) {
        output[0] = (char)(0xe0U | (rune >> 12));
        output[1] = (char)(0x80U | ((rune >> 6) & 0x3fU));
        output[2] = (char)(0x80U | (rune & 0x3fU));
        return 3;
    }
    output[0] = (char)(0xf0U | (rune >> 18));
    output[1] = (char)(0x80U | ((rune >> 12) & 0x3fU));
    output[2] = (char)(0x80U | ((rune >> 6) & 0x3fU));
    output[3] = (char)(0x80U | (rune & 0x3fU));
    return 4;
}

static void foundation_ui_terminal_run(foundation_ui* ui, struct nk_command_buffer* canvas,
                                       const foundation_terminal_cell* cells, uint16_t begin,
                                       uint16_t end, float x, float y, float cell_width,
                                       float line_height) {
    char text[FOUNDATION_UI_TERMINAL_MAX_COLUMNS * 4 + 1];
    size_t length = 0;
    uint16_t column;
    uint16_t foreground = cells[begin].foreground;
    uint16_t background = cells[begin].background;
    const uint8_t attributes = cells[begin].attributes;
    const float width = (float)(end - begin) * cell_width;
    bool visible = false;
    if ((attributes & FOUNDATION_TERMINAL_REVERSE) != 0) {
        const uint16_t swapped = foreground;
        foreground = background;
        background = swapped;
    }
    if ((attributes & FOUNDATION_TERMINAL_BOLD) != 0 && foreground < 8) {
        foreground = (uint16_t)(foreground + 8);
    }
    if (background != FOUNDATION_TERMINAL_DEFAULT_COLOR) {
        nk_fill_rect(canvas, nk_rect(x, y, width, line_height), 0.0f,
                     foundation_ui_terminal_color(ui, background, false));
    }
    for (column = begin; column < end; column++) {
        const uint32_t rune = cells[column].rune == 0 ? ' ' : cells[column].rune;
        if (rune != ' ')
            visible = true;
        length += foundation_ui_terminal_rune(text + length, rune);
    }
    if (visible) {
        struct nk_color color = foundation_ui_terminal_color(ui, foreground, true);
        if ((attributes & FOUNDATION_TERMINAL_DIM) != 0)
            color.a = 150;
        nk_draw_text(canvas, nk_rect(x, y, width, line_height), text, (int)length,
                     &ui->terminal_font->handle, nk_rgba(0, 0, 0, 0), color);
    }
    if ((attributes & FOUNDATION_TERMINAL_UNDERLINE) != 0) {
        nk_stroke_line(canvas, x, y + line_height - 2.0f, x + width, y + line_height - 2.0f, 1.0f,
                       foundation_ui_terminal_color(ui, foreground, true));
    }
}

uint8_t* foundation_ui_terminal_buffer(uint64_t handle, uint64_t length, uint64_t* capacity) {
    foundation_ui* ui = foundation_ui_from(handle);
    uint8_t* storage;
    if (ui == NULL || capacity == NULL || length > SIZE_MAX)
        return NULL;
    if (length > ui->terminal_transfer_capacity) {
        storage = SDL_realloc(ui->terminal_transfer, (size_t)length);
        if (storage == NULL && length != 0)
            return NULL;
        ui->terminal_transfer = storage;
        ui->terminal_transfer_capacity = length;
    }
    *capacity = ui->terminal_transfer_capacity;
    return ui->terminal_transfer;
}

int32_t foundation_ui_terminal_commit(uint64_t handle, uint64_t length) {
    foundation_ui* ui = foundation_ui_from(handle);
    size_t before;
    size_t after;
    if (ui == NULL || length > ui->terminal_transfer_capacity || length > SIZE_MAX) {
        return FOUNDATION_UI_INVALID;
    }
    before = foundation_terminal_history_rows(ui->terminal);
    foundation_terminal_write(ui->terminal, ui->terminal_transfer, (size_t)length);
    after = foundation_terminal_history_rows(ui->terminal);
    if (ui->terminal_scroll != 0 && after > before) {
        ui->terminal_scroll += after - before;
        if (ui->terminal_scroll > after)
            ui->terminal_scroll = after;
    }
    return FOUNDATION_UI_OK;
}

int32_t foundation_ui_terminal(uint64_t handle, float height, fdn_string* input, uint64_t* columns,
                               uint64_t* rows, bool* resized) {
    foundation_ui* ui = foundation_ui_from(handle);
    const struct nk_user_font* font;
    struct nk_command_buffer* canvas;
    struct nk_rect bounds;
    float cell_width;
    float line_height;
    float track_height;
    float thumb_height;
    float thumb_y;
    size_t history;
    uint16_t desired_columns;
    uint16_t desired_rows;
    uint16_t row;
    uint16_t column;
    uint16_t begin;
    if (ui == NULL || input == NULL || columns == NULL || rows == NULL || resized == NULL ||
        !isfinite(height) || height < 48.0f) {
        return FOUNDATION_UI_INVALID;
    }
    nk_layout_row_dynamic(ui->context, height, 1);
    if (nk_widget(&bounds, ui->context) == NK_WIDGET_INVALID) {
        return FOUNDATION_UI_FAILED;
    }
    font = &ui->terminal_font->handle;
    cell_width = font->width(font->userdata, font->height, "M", 1);
    line_height = font->height + 4.0f;
    if (bounds.w <= 22.0f + 2.0f * cell_width) {
        desired_columns = 2;
    } else if (bounds.w >= 22.0f + (float)FOUNDATION_UI_TERMINAL_MAX_COLUMNS * cell_width) {
        desired_columns = FOUNDATION_UI_TERMINAL_MAX_COLUMNS;
    } else {
        desired_columns = (uint16_t)((bounds.w - 22.0f) / cell_width);
    }
    if (bounds.h <= 12.0f + 2.0f * line_height) {
        desired_rows = 2;
    } else if (bounds.h >= 12.0f + (float)FOUNDATION_UI_TERMINAL_MAX_ROWS * line_height) {
        desired_rows = FOUNDATION_UI_TERMINAL_MAX_ROWS;
    } else {
        desired_rows = (uint16_t)((bounds.h - 12.0f) / line_height);
    }
    *resized = desired_columns != foundation_terminal_columns(ui->terminal) ||
               desired_rows != foundation_terminal_rows(ui->terminal);
    if (*resized && !foundation_terminal_resize(ui->terminal, desired_columns, desired_rows)) {
        return FOUNDATION_UI_FAILED;
    }
    history = foundation_terminal_history_rows(ui->terminal);
    if (ui->terminal_scroll > history)
        ui->terminal_scroll = history;
    *columns = desired_columns;
    *rows = desired_rows;
    canvas = nk_window_get_canvas(ui->context);
    nk_fill_rect(canvas, bounds, 6.0f, ui->background);
    for (row = 0; row < desired_rows; row++) {
        const foundation_terminal_cell* line =
            foundation_terminal_view_row(ui->terminal, row, ui->terminal_scroll);
        if (line == NULL)
            continue;
        column = 0;
        while (column < desired_columns) {
            begin = column++;
            while (column < desired_columns && line[column].foreground == line[begin].foreground &&
                   line[column].background == line[begin].background &&
                   line[column].attributes == line[begin].attributes) {
                column++;
            }
            foundation_ui_terminal_run(
                ui, canvas, line, begin, column, bounds.x + 8.0f + (float)begin * cell_width,
                bounds.y + 6.0f + (float)row * line_height, cell_width, line_height);
        }
    }
    if (ui->terminal_focus && ui->terminal_scroll == 0 &&
        foundation_terminal_cursor_visible(ui->terminal)) {
        nk_stroke_rect(
            canvas,
            nk_rect(bounds.x + 8.0f +
                        (float)foundation_terminal_cursor_column(ui->terminal) * cell_width,
                    bounds.y + 6.0f +
                        (float)foundation_terminal_cursor_row(ui->terminal) * line_height,
                    cell_width, line_height),
            0.0f, 1.0f, ui->accent);
    }
    track_height = bounds.h - 12.0f;
    nk_fill_rect(canvas, nk_rect(bounds.x + bounds.w - 6.0f, bounds.y + 6.0f, 2.0f, track_height),
                 1.0f, ui->panel);
    thumb_height = history == 0
                       ? track_height
                       : track_height * (float)desired_rows / (float)(desired_rows + history);
    if (thumb_height < 28.0f)
        thumb_height = 28.0f;
    thumb_y = bounds.y + 6.0f;
    if (history != 0) {
        thumb_y +=
            (track_height - thumb_height) * (1.0f - (float)ui->terminal_scroll / (float)history);
    }
    nk_fill_rect(canvas, nk_rect(bounds.x + bounds.w - 7.0f, thumb_y, 4.0f, thumb_height), 2.0f,
                 ui->muted);
    ui->terminal_bounds = bounds;
    ui->terminal_bounds_valid = true;
    if (ui->terminal_auto_focus) {
        ui->terminal_auto_focus = false;
        ui->terminal_focus = true;
        (void)SDL_StartTextInput(ui->window);
    }
    fdn_string_drop(input);
    *input = foundation_runtime_string_copy(
        &(fdn_string){ui->terminal_input, ui->terminal_input_length, 0});
    ui->terminal_input_length = 0;
    return FOUNDATION_UI_OK;
}
