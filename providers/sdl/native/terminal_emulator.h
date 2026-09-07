#ifndef FOUNDATION_UI_TERMINAL_H
#define FOUNDATION_UI_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FOUNDATION_TERMINAL_DEFAULT_COLOR = 256,
};

enum foundation_terminal_attribute {
    FOUNDATION_TERMINAL_BOLD = 1,
    FOUNDATION_TERMINAL_DIM = 2,
    FOUNDATION_TERMINAL_UNDERLINE = 4,
    FOUNDATION_TERMINAL_REVERSE = 8,
};

typedef struct foundation_terminal_cell {
    uint32_t rune;
    uint16_t foreground;
    uint16_t background;
    uint8_t attributes;
} foundation_terminal_cell;

typedef struct foundation_terminal foundation_terminal;

foundation_terminal* foundation_terminal_create(uint16_t columns, uint16_t rows);
void foundation_terminal_destroy(foundation_terminal* terminal);
bool foundation_terminal_resize(foundation_terminal* terminal, uint16_t columns, uint16_t rows);
void foundation_terminal_write(foundation_terminal* terminal, const uint8_t* source, size_t length);

const foundation_terminal_cell* foundation_terminal_cells(const foundation_terminal* terminal);
const foundation_terminal_cell* foundation_terminal_view_row(const foundation_terminal* terminal,
                                                             uint16_t row, size_t scroll_offset);
size_t foundation_terminal_history_rows(const foundation_terminal* terminal);
uint16_t foundation_terminal_columns(const foundation_terminal* terminal);
uint16_t foundation_terminal_rows(const foundation_terminal* terminal);
uint16_t foundation_terminal_cursor_column(const foundation_terminal* terminal);
uint16_t foundation_terminal_cursor_row(const foundation_terminal* terminal);
bool foundation_terminal_cursor_visible(const foundation_terminal* terminal);

#endif
