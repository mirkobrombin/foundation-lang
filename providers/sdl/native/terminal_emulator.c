#include "terminal_emulator.h"

#include <stdlib.h>
#include <string.h>

#define FOUNDATION_TERMINAL_MAX_COLUMNS 240
#define FOUNDATION_TERMINAL_MAX_ROWS 120
#define FOUNDATION_TERMINAL_PARAMETER_CAPACITY 16
#define FOUNDATION_TERMINAL_HISTORY_CAPACITY 2048

enum foundation_terminal_parser_state {
    FOUNDATION_TERMINAL_TEXT,
    FOUNDATION_TERMINAL_ESCAPE,
    FOUNDATION_TERMINAL_CSI,
    FOUNDATION_TERMINAL_OSC,
    FOUNDATION_TERMINAL_OSC_ESCAPE,
    FOUNDATION_TERMINAL_CHARSET,
};

struct foundation_terminal {
    foundation_terminal_cell* cells;
    foundation_terminal_cell* primary_cells;
    foundation_terminal_cell* alternate_cells;
    foundation_terminal_cell* history;
    uint16_t columns;
    uint16_t rows;
    uint16_t cursor_column;
    uint16_t cursor_row;
    uint16_t saved_column;
    uint16_t saved_row;
    uint16_t primary_column;
    uint16_t primary_row;
    uint16_t scroll_top;
    uint16_t scroll_bottom;
    uint16_t foreground;
    uint16_t background;
    uint8_t attributes;
    uint8_t parser_state;
    uint8_t utf8_pending;
    uint8_t utf8_total;
    uint32_t utf8_rune;
    uint32_t parameters[FOUNDATION_TERMINAL_PARAMETER_CAPACITY];
    uint8_t parameter_count;
    uint8_t charset_target;
    uint8_t active_charset;
    bool parameter_set;
    bool csi_private;
    bool cursor_visible;
    bool g0_graphics;
    bool g1_graphics;
    bool alternate_active;
    size_t history_start;
    size_t history_count;
};

static foundation_terminal_cell foundation_terminal_blank(void) {
    return (foundation_terminal_cell){
        .rune = ' ',
        .foreground = FOUNDATION_TERMINAL_DEFAULT_COLOR,
        .background = FOUNDATION_TERMINAL_DEFAULT_COLOR,
        .attributes = 0,
    };
}

static void foundation_terminal_clear_cells(foundation_terminal_cell* cells, size_t count) {
    const foundation_terminal_cell blank = foundation_terminal_blank();
    size_t index;
    for (index = 0; index < count; index++)
        cells[index] = blank;
}

static foundation_terminal_cell* foundation_terminal_cell_at(foundation_terminal* terminal,
                                                             uint16_t column, uint16_t row) {
    return &terminal->cells[(size_t)row * terminal->columns + column];
}

static void foundation_terminal_clear_row(foundation_terminal* terminal, uint16_t row,
                                          uint16_t begin, uint16_t end) {
    uint16_t column;
    if (row >= terminal->rows || begin >= terminal->columns)
        return;
    if (end > terminal->columns)
        end = terminal->columns;
    for (column = begin; column < end; column++) {
        *foundation_terminal_cell_at(terminal, column, row) = foundation_terminal_blank();
    }
}

static void foundation_terminal_remember_row(foundation_terminal* terminal,
                                             const foundation_terminal_cell* row) {
    size_t index;
    if (terminal->alternate_active)
        return;
    if (terminal->history == NULL) {
        terminal->history = malloc(FOUNDATION_TERMINAL_HISTORY_CAPACITY *
                                   (size_t)terminal->columns * sizeof(*terminal->history));
        if (terminal->history == NULL)
            return;
    }
    if (terminal->history_count == FOUNDATION_TERMINAL_HISTORY_CAPACITY) {
        terminal->history_start =
            (terminal->history_start + 1) % FOUNDATION_TERMINAL_HISTORY_CAPACITY;
        terminal->history_count--;
    }
    index =
        (terminal->history_start + terminal->history_count) % FOUNDATION_TERMINAL_HISTORY_CAPACITY;
    memcpy(terminal->history + index * terminal->columns, row,
           (size_t)terminal->columns * sizeof(*row));
    terminal->history_count++;
}

static void foundation_terminal_scroll_up(foundation_terminal* terminal, uint16_t begin,
                                          uint16_t end, uint32_t amount) {
    const size_t row_bytes = (size_t)terminal->columns * sizeof(*terminal->cells);
    uint16_t rows;
    if (begin >= terminal->rows || end >= terminal->rows || begin > end)
        return;
    rows = (uint16_t)(end - begin + 1);
    if (amount > rows)
        amount = rows;
    while (amount-- != 0) {
        if (begin == 0 && end == terminal->rows - 1)
            foundation_terminal_remember_row(terminal, terminal->cells);
        if (rows > 1) {
            memmove(terminal->cells + (size_t)begin * terminal->columns,
                    terminal->cells + (size_t)(begin + 1) * terminal->columns,
                    (size_t)(rows - 1) * row_bytes);
        }
        foundation_terminal_clear_row(terminal, end, 0, terminal->columns);
    }
}

static void foundation_terminal_scroll_down(foundation_terminal* terminal, uint16_t begin,
                                            uint16_t end, uint32_t amount) {
    const size_t row_bytes = (size_t)terminal->columns * sizeof(*terminal->cells);
    uint16_t rows;
    if (begin >= terminal->rows || end >= terminal->rows || begin > end)
        return;
    rows = (uint16_t)(end - begin + 1);
    if (amount > rows)
        amount = rows;
    while (amount-- != 0) {
        if (rows > 1) {
            memmove(terminal->cells + (size_t)(begin + 1) * terminal->columns,
                    terminal->cells + (size_t)begin * terminal->columns,
                    (size_t)(rows - 1) * row_bytes);
        }
        foundation_terminal_clear_row(terminal, begin, 0, terminal->columns);
    }
}

static void foundation_terminal_line_feed(foundation_terminal* terminal) {
    if (terminal->cursor_row == terminal->scroll_bottom) {
        foundation_terminal_scroll_up(terminal, terminal->scroll_top, terminal->scroll_bottom, 1);
    } else if (terminal->cursor_row + 1 < terminal->rows) {
        terminal->cursor_row++;
    }
}

static uint32_t foundation_terminal_graphics_rune(uint32_t rune) {
    switch (rune) {
    case 'j':
        return 0x2518;
    case 'k':
        return 0x2510;
    case 'l':
        return 0x250c;
    case 'm':
        return 0x2514;
    case 'n':
        return 0x253c;
    case 'q':
        return 0x2500;
    case 't':
        return 0x251c;
    case 'u':
        return 0x2524;
    case 'v':
        return 0x2534;
    case 'w':
        return 0x252c;
    case 'x':
        return 0x2502;
    default:
        return rune;
    }
}

static void foundation_terminal_put(foundation_terminal* terminal, uint32_t rune) {
    foundation_terminal_cell* cell;
    if (terminal->cursor_column >= terminal->columns) {
        terminal->cursor_column = 0;
        foundation_terminal_line_feed(terminal);
    }
    if ((terminal->active_charset == 0 && terminal->g0_graphics) ||
        (terminal->active_charset == 1 && terminal->g1_graphics)) {
        rune = foundation_terminal_graphics_rune(rune);
    }
    cell = foundation_terminal_cell_at(terminal, terminal->cursor_column, terminal->cursor_row);
    cell->rune = rune;
    cell->foreground = terminal->foreground;
    cell->background = terminal->background;
    cell->attributes = terminal->attributes;
    terminal->cursor_column++;
}

static uint32_t foundation_terminal_parameter(const foundation_terminal* terminal, uint8_t index,
                                              uint32_t fallback) {
    if (index >= terminal->parameter_count)
        return fallback;
    return terminal->parameters[index] == 0 ? fallback : terminal->parameters[index];
}

static uint16_t foundation_terminal_clamp_position(uint32_t value, uint16_t limit) {
    if (limit == 0)
        return 0;
    if (value == 0)
        return 0;
    if (value > limit)
        return (uint16_t)(limit - 1);
    return (uint16_t)(value - 1);
}

static void foundation_terminal_erase_display(foundation_terminal* terminal, uint32_t mode) {
    uint16_t row;
    if (mode == 2 || mode == 3) {
        foundation_terminal_clear_cells(terminal->cells,
                                        (size_t)terminal->columns * terminal->rows);
        if (mode == 3) {
            terminal->history_start = 0;
            terminal->history_count = 0;
        }
        return;
    }
    if (mode == 1) {
        for (row = 0; row < terminal->cursor_row; row++) {
            foundation_terminal_clear_row(terminal, row, 0, terminal->columns);
        }
        foundation_terminal_clear_row(terminal, terminal->cursor_row, 0,
                                      (uint16_t)(terminal->cursor_column + 1));
        return;
    }
    foundation_terminal_clear_row(terminal, terminal->cursor_row, terminal->cursor_column,
                                  terminal->columns);
    for (row = (uint16_t)(terminal->cursor_row + 1); row < terminal->rows; row++) {
        foundation_terminal_clear_row(terminal, row, 0, terminal->columns);
    }
}

static void foundation_terminal_erase_line(foundation_terminal* terminal, uint32_t mode) {
    if (mode == 2) {
        foundation_terminal_clear_row(terminal, terminal->cursor_row, 0, terminal->columns);
    } else if (mode == 1) {
        foundation_terminal_clear_row(terminal, terminal->cursor_row, 0,
                                      (uint16_t)(terminal->cursor_column + 1));
    } else {
        foundation_terminal_clear_row(terminal, terminal->cursor_row, terminal->cursor_column,
                                      terminal->columns);
    }
}

static void foundation_terminal_insert_characters(foundation_terminal* terminal, uint32_t amount) {
    foundation_terminal_cell* row = foundation_terminal_cell_at(terminal, 0, terminal->cursor_row);
    uint16_t available = (uint16_t)(terminal->columns - terminal->cursor_column);
    if (amount > available)
        amount = available;
    if (amount < available) {
        memmove(row + terminal->cursor_column + amount, row + terminal->cursor_column,
                (size_t)(available - amount) * sizeof(*row));
    }
    foundation_terminal_clear_row(terminal, terminal->cursor_row, terminal->cursor_column,
                                  (uint16_t)(terminal->cursor_column + amount));
}

static void foundation_terminal_delete_characters(foundation_terminal* terminal, uint32_t amount) {
    foundation_terminal_cell* row = foundation_terminal_cell_at(terminal, 0, terminal->cursor_row);
    uint16_t available = (uint16_t)(terminal->columns - terminal->cursor_column);
    if (amount > available)
        amount = available;
    if (amount < available) {
        memmove(row + terminal->cursor_column, row + terminal->cursor_column + amount,
                (size_t)(available - amount) * sizeof(*row));
    }
    foundation_terminal_clear_row(terminal, terminal->cursor_row,
                                  (uint16_t)(terminal->columns - amount), terminal->columns);
}

static void foundation_terminal_erase_characters(foundation_terminal* terminal, uint32_t amount) {
    uint32_t end = terminal->cursor_column + amount;
    if (end > terminal->columns)
        end = terminal->columns;
    foundation_terminal_clear_row(terminal, terminal->cursor_row, terminal->cursor_column,
                                  (uint16_t)end);
}

static bool foundation_terminal_enter_alternate(foundation_terminal* terminal) {
    if (terminal->alternate_active)
        return true;
    if (terminal->alternate_cells == NULL) {
        terminal->alternate_cells =
            malloc((size_t)terminal->columns * terminal->rows * sizeof(*terminal->alternate_cells));
        if (terminal->alternate_cells == NULL)
            return false;
    }
    terminal->primary_column = terminal->cursor_column;
    terminal->primary_row = terminal->cursor_row;
    terminal->cells = terminal->alternate_cells;
    terminal->alternate_active = true;
    foundation_terminal_clear_cells(terminal->cells, (size_t)terminal->columns * terminal->rows);
    terminal->cursor_column = 0;
    terminal->cursor_row = 0;
    terminal->scroll_top = 0;
    terminal->scroll_bottom = (uint16_t)(terminal->rows - 1);
    return true;
}

static void foundation_terminal_leave_alternate(foundation_terminal* terminal) {
    if (!terminal->alternate_active)
        return;
    terminal->cells = terminal->primary_cells;
    terminal->alternate_active = false;
    terminal->cursor_column = terminal->primary_column < terminal->columns
                                  ? terminal->primary_column
                                  : (uint16_t)(terminal->columns - 1);
    terminal->cursor_row = terminal->primary_row < terminal->rows ? terminal->primary_row
                                                                  : (uint16_t)(terminal->rows - 1);
    terminal->scroll_top = 0;
    terminal->scroll_bottom = (uint16_t)(terminal->rows - 1);
}

static void foundation_terminal_private_mode(foundation_terminal* terminal, uint8_t byte) {
    uint8_t index;
    const bool enabled = byte == 'h';
    for (index = 0; index < terminal->parameter_count; index++) {
        const uint32_t mode = terminal->parameters[index];
        if (mode == 25) {
            terminal->cursor_visible = enabled;
        } else if (mode == 47 || mode == 1047 || mode == 1049) {
            if (enabled)
                (void)foundation_terminal_enter_alternate(terminal);
            else
                foundation_terminal_leave_alternate(terminal);
        }
    }
}

static void foundation_terminal_sgr(foundation_terminal* terminal) {
    uint8_t index = 0;
    if (terminal->parameter_count == 0) {
        terminal->parameters[0] = 0;
        terminal->parameter_count = 1;
    }
    while (index < terminal->parameter_count) {
        const uint32_t value = terminal->parameters[index++];
        if (value == 0) {
            terminal->foreground = FOUNDATION_TERMINAL_DEFAULT_COLOR;
            terminal->background = FOUNDATION_TERMINAL_DEFAULT_COLOR;
            terminal->attributes = 0;
        } else if (value == 1) {
            terminal->attributes |= FOUNDATION_TERMINAL_BOLD;
        } else if (value == 2) {
            terminal->attributes |= FOUNDATION_TERMINAL_DIM;
        } else if (value == 4) {
            terminal->attributes |= FOUNDATION_TERMINAL_UNDERLINE;
        } else if (value == 7) {
            terminal->attributes |= FOUNDATION_TERMINAL_REVERSE;
        } else if (value == 22) {
            terminal->attributes &= (uint8_t)~(FOUNDATION_TERMINAL_BOLD | FOUNDATION_TERMINAL_DIM);
        } else if (value == 24) {
            terminal->attributes &= (uint8_t)~FOUNDATION_TERMINAL_UNDERLINE;
        } else if (value == 27) {
            terminal->attributes &= (uint8_t)~FOUNDATION_TERMINAL_REVERSE;
        } else if (value >= 30 && value <= 37) {
            terminal->foreground = (uint16_t)(value - 30);
        } else if (value == 39) {
            terminal->foreground = FOUNDATION_TERMINAL_DEFAULT_COLOR;
        } else if (value >= 40 && value <= 47) {
            terminal->background = (uint16_t)(value - 40);
        } else if (value == 49) {
            terminal->background = FOUNDATION_TERMINAL_DEFAULT_COLOR;
        } else if (value >= 90 && value <= 97) {
            terminal->foreground = (uint16_t)(value - 90 + 8);
        } else if (value >= 100 && value <= 107) {
            terminal->background = (uint16_t)(value - 100 + 8);
        } else if ((value == 38 || value == 48) && index + 1 < terminal->parameter_count &&
                   terminal->parameters[index] == 5) {
            const uint16_t color = (uint16_t)(terminal->parameters[index + 1] & 255U);
            if (value == 38)
                terminal->foreground = color;
            if (value == 48)
                terminal->background = color;
            index += 2;
        }
    }
}

static void foundation_terminal_finish_csi(foundation_terminal* terminal, uint8_t byte) {
    const uint32_t first = foundation_terminal_parameter(terminal, 0, 1);
    uint32_t amount;
    if (byte == 'A') {
        amount = first;
        terminal->cursor_row =
            amount > terminal->cursor_row ? 0 : (uint16_t)(terminal->cursor_row - amount);
    } else if (byte == 'B') {
        amount = first;
        terminal->cursor_row = amount >= (uint32_t)(terminal->rows - terminal->cursor_row)
                                   ? (uint16_t)(terminal->rows - 1)
                                   : (uint16_t)(terminal->cursor_row + amount);
    } else if (byte == 'C') {
        amount = first;
        terminal->cursor_column = amount >= (uint32_t)(terminal->columns - terminal->cursor_column)
                                      ? (uint16_t)(terminal->columns - 1)
                                      : (uint16_t)(terminal->cursor_column + amount);
    } else if (byte == 'D') {
        amount = first;
        terminal->cursor_column =
            amount > terminal->cursor_column ? 0 : (uint16_t)(terminal->cursor_column - amount);
    } else if (byte == 'E') {
        amount = first;
        terminal->cursor_row = amount >= (uint32_t)(terminal->rows - terminal->cursor_row)
                                   ? (uint16_t)(terminal->rows - 1)
                                   : (uint16_t)(terminal->cursor_row + amount);
        terminal->cursor_column = 0;
    } else if (byte == 'F') {
        amount = first;
        terminal->cursor_row =
            amount > terminal->cursor_row ? 0 : (uint16_t)(terminal->cursor_row - amount);
        terminal->cursor_column = 0;
    } else if (byte == 'G') {
        terminal->cursor_column = foundation_terminal_clamp_position(first, terminal->columns);
    } else if (byte == 'd') {
        terminal->cursor_row = foundation_terminal_clamp_position(first, terminal->rows);
    } else if (byte == 'a') {
        amount = first;
        terminal->cursor_column = amount >= (uint32_t)(terminal->columns - terminal->cursor_column)
                                      ? (uint16_t)(terminal->columns - 1)
                                      : (uint16_t)(terminal->cursor_column + amount);
    } else if (byte == 'e') {
        amount = first;
        terminal->cursor_row = amount >= (uint32_t)(terminal->rows - terminal->cursor_row)
                                   ? (uint16_t)(terminal->rows - 1)
                                   : (uint16_t)(terminal->cursor_row + amount);
    } else if (byte == 'H' || byte == 'f') {
        terminal->cursor_row = foundation_terminal_clamp_position(
            foundation_terminal_parameter(terminal, 0, 1), terminal->rows);
        terminal->cursor_column = foundation_terminal_clamp_position(
            foundation_terminal_parameter(terminal, 1, 1), terminal->columns);
    } else if (byte == 'J') {
        foundation_terminal_erase_display(
            terminal, terminal->parameter_count == 0 ? 0 : terminal->parameters[0]);
    } else if (byte == 'K') {
        foundation_terminal_erase_line(
            terminal, terminal->parameter_count == 0 ? 0 : terminal->parameters[0]);
    } else if (byte == 'm') {
        foundation_terminal_sgr(terminal);
    } else if (byte == '@') {
        foundation_terminal_insert_characters(terminal, first);
    } else if (byte == 'P') {
        foundation_terminal_delete_characters(terminal, first);
    } else if (byte == 'X') {
        foundation_terminal_erase_characters(terminal, first);
    } else if (byte == 'L' && terminal->cursor_row >= terminal->scroll_top &&
               terminal->cursor_row <= terminal->scroll_bottom) {
        foundation_terminal_scroll_down(terminal, terminal->cursor_row, terminal->scroll_bottom,
                                        first);
    } else if (byte == 'M' && terminal->cursor_row >= terminal->scroll_top &&
               terminal->cursor_row <= terminal->scroll_bottom) {
        foundation_terminal_scroll_up(terminal, terminal->cursor_row, terminal->scroll_bottom,
                                      first);
    } else if (byte == 'S') {
        foundation_terminal_scroll_up(terminal, terminal->scroll_top, terminal->scroll_bottom,
                                      first);
    } else if (byte == 'T') {
        foundation_terminal_scroll_down(terminal, terminal->scroll_top, terminal->scroll_bottom,
                                        first);
    } else if (byte == 'r' && !terminal->csi_private) {
        const uint16_t top = foundation_terminal_clamp_position(
            foundation_terminal_parameter(terminal, 0, 1), terminal->rows);
        const uint16_t bottom = foundation_terminal_clamp_position(
            foundation_terminal_parameter(terminal, 1, terminal->rows), terminal->rows);
        if (top < bottom) {
            terminal->scroll_top = top;
            terminal->scroll_bottom = bottom;
            terminal->cursor_column = 0;
            terminal->cursor_row = 0;
        }
    } else if (byte == 's') {
        terminal->saved_column = terminal->cursor_column;
        terminal->saved_row = terminal->cursor_row;
    } else if (byte == 'u') {
        terminal->cursor_column = terminal->saved_column < terminal->columns
                                      ? terminal->saved_column
                                      : (uint16_t)(terminal->columns - 1);
        terminal->cursor_row = terminal->saved_row < terminal->rows
                                   ? terminal->saved_row
                                   : (uint16_t)(terminal->rows - 1);
    } else if (terminal->csi_private && (byte == 'h' || byte == 'l')) {
        foundation_terminal_private_mode(terminal, byte);
    }
    terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
    terminal->parameter_count = 0;
    terminal->parameter_set = false;
    terminal->csi_private = false;
}

static void foundation_terminal_csi(foundation_terminal* terminal, uint8_t byte) {
    if (byte == '?' && terminal->parameter_count == 0 && !terminal->parameter_set) {
        terminal->csi_private = true;
        return;
    }
    if (byte >= '0' && byte <= '9') {
        if (terminal->parameter_count == 0)
            terminal->parameter_count = 1;
        terminal->parameters[terminal->parameter_count - 1] =
            terminal->parameters[terminal->parameter_count - 1] * 10U + (uint32_t)(byte - '0');
        terminal->parameter_set = true;
        return;
    }
    if (byte == ';') {
        if (terminal->parameter_count == 0)
            terminal->parameter_count = 1;
        if (terminal->parameter_count < FOUNDATION_TERMINAL_PARAMETER_CAPACITY) {
            terminal->parameters[terminal->parameter_count++] = 0;
        }
        terminal->parameter_set = false;
        return;
    }
    if (byte >= 0x40 && byte <= 0x7e) {
        foundation_terminal_finish_csi(terminal, byte);
    }
}

static void foundation_terminal_text(foundation_terminal* terminal, uint8_t byte) {
    if (terminal->utf8_pending != 0) {
        if ((byte & 0xc0U) != 0x80U) {
            terminal->utf8_pending = 0;
            terminal->utf8_total = 0;
            foundation_terminal_put(terminal, 0xfffd);
            foundation_terminal_text(terminal, byte);
            return;
        }
        terminal->utf8_rune = (terminal->utf8_rune << 6) | (byte & 0x3fU);
        terminal->utf8_pending--;
        if (terminal->utf8_pending == 0) {
            const uint32_t rune = terminal->utf8_rune;
            const bool overlong = (terminal->utf8_total == 2 && rune < 0x80) ||
                                  (terminal->utf8_total == 3 && rune < 0x800) ||
                                  (terminal->utf8_total == 4 && rune < 0x10000);
            const bool surrogate = rune >= 0xd800 && rune <= 0xdfff;
            foundation_terminal_put(terminal,
                                    overlong || surrogate || rune > 0x10ffff ? 0xfffd : rune);
            terminal->utf8_total = 0;
        }
        return;
    }
    if (byte == 0x1b) {
        terminal->parser_state = FOUNDATION_TERMINAL_ESCAPE;
    } else if (byte == '\r') {
        terminal->cursor_column = 0;
    } else if (byte == '\n' || byte == '\v' || byte == '\f') {
        foundation_terminal_line_feed(terminal);
    } else if (byte == '\b') {
        if (terminal->cursor_column != 0)
            terminal->cursor_column--;
    } else if (byte == '\t') {
        uint16_t target = (uint16_t)((terminal->cursor_column + 8U) & ~7U);
        if (target >= terminal->columns)
            target = (uint16_t)(terminal->columns - 1);
        terminal->cursor_column = target;
    } else if (byte == 0x0e) {
        terminal->active_charset = 1;
    } else if (byte == 0x0f) {
        terminal->active_charset = 0;
    } else if (byte >= 0x20 && byte < 0x7f) {
        foundation_terminal_put(terminal, byte);
    } else if (byte >= 0xc2 && byte <= 0xdf) {
        terminal->utf8_rune = byte & 0x1fU;
        terminal->utf8_pending = 1;
        terminal->utf8_total = 2;
    } else if (byte >= 0xe0 && byte <= 0xef) {
        terminal->utf8_rune = byte & 0x0fU;
        terminal->utf8_pending = 2;
        terminal->utf8_total = 3;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
        terminal->utf8_rune = byte & 0x07U;
        terminal->utf8_pending = 3;
        terminal->utf8_total = 4;
    } else if (byte >= 0x80) {
        foundation_terminal_put(terminal, 0xfffd);
    }
}

foundation_terminal* foundation_terminal_create(uint16_t columns, uint16_t rows) {
    foundation_terminal* terminal;
    if (columns == 0 || rows == 0 || columns > FOUNDATION_TERMINAL_MAX_COLUMNS ||
        rows > FOUNDATION_TERMINAL_MAX_ROWS) {
        return NULL;
    }
    terminal = calloc(1, sizeof(*terminal));
    if (terminal == NULL)
        return NULL;
    terminal->primary_cells = malloc((size_t)columns * rows * sizeof(*terminal->primary_cells));
    if (terminal->primary_cells == NULL) {
        free(terminal);
        return NULL;
    }
    terminal->cells = terminal->primary_cells;
    terminal->columns = columns;
    terminal->rows = rows;
    terminal->scroll_bottom = (uint16_t)(rows - 1);
    terminal->foreground = FOUNDATION_TERMINAL_DEFAULT_COLOR;
    terminal->background = FOUNDATION_TERMINAL_DEFAULT_COLOR;
    terminal->cursor_visible = true;
    foundation_terminal_clear_cells(terminal->cells, (size_t)columns * rows);
    return terminal;
}

void foundation_terminal_destroy(foundation_terminal* terminal) {
    if (terminal == NULL)
        return;
    free(terminal->primary_cells);
    free(terminal->alternate_cells);
    free(terminal->history);
    free(terminal);
}

bool foundation_terminal_resize(foundation_terminal* terminal, uint16_t columns, uint16_t rows) {
    foundation_terminal_cell* primary;
    foundation_terminal_cell* alternate = NULL;
    uint16_t copy_columns;
    uint16_t copy_rows;
    uint16_t row;
    if (terminal == NULL || columns == 0 || rows == 0 ||
        columns > FOUNDATION_TERMINAL_MAX_COLUMNS || rows > FOUNDATION_TERMINAL_MAX_ROWS) {
        return false;
    }
    if (terminal->columns == columns && terminal->rows == rows)
        return true;
    primary = malloc((size_t)columns * rows * sizeof(*primary));
    if (primary == NULL)
        return false;
    foundation_terminal_clear_cells(primary, (size_t)columns * rows);
    copy_columns = columns < terminal->columns ? columns : terminal->columns;
    copy_rows = rows < terminal->rows ? rows : terminal->rows;
    for (row = 0; row < copy_rows; row++) {
        memcpy(primary + (size_t)row * columns,
               terminal->primary_cells + (size_t)row * terminal->columns,
               (size_t)copy_columns * sizeof(*primary));
    }
    if (terminal->alternate_cells != NULL) {
        alternate = malloc((size_t)columns * rows * sizeof(*alternate));
        if (alternate == NULL) {
            free(primary);
            return false;
        }
        foundation_terminal_clear_cells(alternate, (size_t)columns * rows);
        for (row = 0; row < copy_rows; row++) {
            memcpy(alternate + (size_t)row * columns,
                   terminal->alternate_cells + (size_t)row * terminal->columns,
                   (size_t)copy_columns * sizeof(*alternate));
        }
    }
    free(terminal->primary_cells);
    free(terminal->alternate_cells);
    terminal->primary_cells = primary;
    terminal->alternate_cells = alternate;
    terminal->cells = terminal->alternate_active ? alternate : primary;
    if (terminal->columns != columns) {
        free(terminal->history);
        terminal->history = NULL;
        terminal->history_start = 0;
        terminal->history_count = 0;
    }
    terminal->columns = columns;
    terminal->rows = rows;
    terminal->scroll_top = 0;
    terminal->scroll_bottom = (uint16_t)(rows - 1);
    if (terminal->cursor_column >= columns)
        terminal->cursor_column = columns - 1;
    if (terminal->cursor_row >= rows)
        terminal->cursor_row = rows - 1;
    if (terminal->primary_column >= columns)
        terminal->primary_column = columns - 1;
    if (terminal->primary_row >= rows)
        terminal->primary_row = rows - 1;
    return true;
}

void foundation_terminal_write(foundation_terminal* terminal, const uint8_t* source,
                               size_t length) {
    size_t index;
    if (terminal == NULL || (source == NULL && length != 0))
        return;
    for (index = 0; index < length; index++) {
        const uint8_t byte = source[index];
        if (terminal->parser_state == FOUNDATION_TERMINAL_TEXT) {
            foundation_terminal_text(terminal, byte);
        } else if (terminal->parser_state == FOUNDATION_TERMINAL_ESCAPE) {
            if (byte == '[') {
                terminal->parser_state = FOUNDATION_TERMINAL_CSI;
                memset(terminal->parameters, 0, sizeof(terminal->parameters));
                terminal->parameter_count = 0;
                terminal->parameter_set = false;
                terminal->csi_private = false;
            } else if (byte == ']') {
                terminal->parser_state = FOUNDATION_TERMINAL_OSC;
            } else if (byte == '(' || byte == ')' || byte == '*' || byte == '+') {
                terminal->charset_target = byte == '(' ? 0 : 1;
                terminal->parser_state = FOUNDATION_TERMINAL_CHARSET;
            } else if (byte == '#') {
                terminal->charset_target = 2;
                terminal->parser_state = FOUNDATION_TERMINAL_CHARSET;
            } else if (byte == '7') {
                terminal->saved_column = terminal->cursor_column;
                terminal->saved_row = terminal->cursor_row;
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else if (byte == '8') {
                terminal->cursor_column = terminal->saved_column;
                terminal->cursor_row = terminal->saved_row;
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else if (byte == 'D') {
                foundation_terminal_line_feed(terminal);
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else if (byte == 'E') {
                terminal->cursor_column = 0;
                foundation_terminal_line_feed(terminal);
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else if (byte == 'M') {
                if (terminal->cursor_row == terminal->scroll_top) {
                    foundation_terminal_scroll_down(terminal, terminal->scroll_top,
                                                    terminal->scroll_bottom, 1);
                } else if (terminal->cursor_row != 0) {
                    terminal->cursor_row--;
                }
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else if (byte == 'c') {
                foundation_terminal_clear_cells(terminal->cells,
                                                (size_t)terminal->columns * terminal->rows);
                terminal->cursor_column = 0;
                terminal->cursor_row = 0;
                terminal->foreground = FOUNDATION_TERMINAL_DEFAULT_COLOR;
                terminal->background = FOUNDATION_TERMINAL_DEFAULT_COLOR;
                terminal->attributes = 0;
                terminal->g0_graphics = false;
                terminal->g1_graphics = false;
                terminal->active_charset = 0;
                terminal->scroll_top = 0;
                terminal->scroll_bottom = (uint16_t)(terminal->rows - 1);
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            } else {
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            }
        } else if (terminal->parser_state == FOUNDATION_TERMINAL_CSI) {
            foundation_terminal_csi(terminal, byte);
        } else if (terminal->parser_state == FOUNDATION_TERMINAL_OSC) {
            if (byte == 0x07)
                terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
            if (byte == 0x1b)
                terminal->parser_state = FOUNDATION_TERMINAL_OSC_ESCAPE;
        } else if (terminal->parser_state == FOUNDATION_TERMINAL_OSC_ESCAPE) {
            terminal->parser_state =
                byte == '\\' ? FOUNDATION_TERMINAL_TEXT : FOUNDATION_TERMINAL_OSC;
        } else if (terminal->parser_state == FOUNDATION_TERMINAL_CHARSET) {
            if (terminal->charset_target == 0)
                terminal->g0_graphics = byte == '0';
            if (terminal->charset_target == 1)
                terminal->g1_graphics = byte == '0';
            terminal->parser_state = FOUNDATION_TERMINAL_TEXT;
        }
    }
}

const foundation_terminal_cell* foundation_terminal_cells(const foundation_terminal* terminal) {
    return terminal == NULL ? NULL : terminal->cells;
}

const foundation_terminal_cell* foundation_terminal_view_row(const foundation_terminal* terminal,
                                                             uint16_t row, size_t scroll_offset) {
    size_t total;
    size_t start;
    size_t selected;
    size_t history_index;
    if (terminal == NULL || row >= terminal->rows)
        return NULL;
    if (terminal->alternate_active || terminal->history_count == 0)
        return terminal->cells + (size_t)row * terminal->columns;
    if (scroll_offset > terminal->history_count)
        scroll_offset = terminal->history_count;
    total = terminal->history_count + terminal->rows;
    start = total - terminal->rows - scroll_offset;
    selected = start + row;
    if (selected >= terminal->history_count) {
        return terminal->cells + (selected - terminal->history_count) * terminal->columns;
    }
    history_index = (terminal->history_start + selected) % FOUNDATION_TERMINAL_HISTORY_CAPACITY;
    return terminal->history + history_index * terminal->columns;
}

size_t foundation_terminal_history_rows(const foundation_terminal* terminal) {
    return terminal == NULL || terminal->alternate_active ? 0 : terminal->history_count;
}

uint16_t foundation_terminal_columns(const foundation_terminal* terminal) {
    return terminal == NULL ? 0 : terminal->columns;
}

uint16_t foundation_terminal_rows(const foundation_terminal* terminal) {
    return terminal == NULL ? 0 : terminal->rows;
}

uint16_t foundation_terminal_cursor_column(const foundation_terminal* terminal) {
    return terminal == NULL ? 0 : terminal->cursor_column;
}

uint16_t foundation_terminal_cursor_row(const foundation_terminal* terminal) {
    return terminal == NULL ? 0 : terminal->cursor_row;
}

bool foundation_terminal_cursor_visible(const foundation_terminal* terminal) {
    return terminal != NULL && terminal->cursor_visible;
}
