#include "foundation/runtime.h"

#include <stdio.h>

void fdn_println(const char *value) {
    fputs(value, stdout);
    fputc('\n', stdout);
}
