#include "terminal_emulator.h"

#include <stdint.h>

int main(void) {
    const uint8_t input[] = {'A',  0xc0, 0x80, 'B',  0xed, 0xa0, 0x80, 'C',  0xf4, 0x90,
                             0x80, 0x80, 'D',  0xf0, 0x9f, 0x98, 0x80, 0xe2, 'X'};
    const uint32_t expected[] = {'A',    0xfffd, 0xfffd,  'B',    0xfffd, 'C',
                                 0xfffd, 'D',    0x1f600, 0xfffd, 'X'};
    foundation_terminal* terminal = foundation_terminal_create(16, 2);
    const foundation_terminal_cell* cells;
    size_t index;
    if (terminal == NULL)
        return 1;
    foundation_terminal_write(terminal, input, sizeof(input));
    cells = foundation_terminal_cells(terminal);
    if (cells == NULL) {
        foundation_terminal_destroy(terminal);
        return 2;
    }
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); index++) {
        if (cells[index].rune != expected[index]) {
            foundation_terminal_destroy(terminal);
            return 3;
        }
    }
    foundation_terminal_destroy(terminal);
    return 0;
}
