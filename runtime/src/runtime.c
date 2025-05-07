#include "foundation/runtime.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void fdn_arithmetic_panic(const char *message) {
    fputs("foundation: arithmetic error: ", stderr);
    fputs(message, stderr);
    fputc('\n', stderr);
    abort();
}

static int32_t fdn_i32_checked(int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) {
        fdn_arithmetic_panic("i32 overflow");
    }
    return (int32_t)value;
}

void fdn_println(const char *value) {
    fputs(value, stdout);
    fputc('\n', stdout);
}

int32_t fdn_i32_add(int32_t left, int32_t right) {
    return fdn_i32_checked((int64_t)left + (int64_t)right);
}

int32_t fdn_i32_subtract(int32_t left, int32_t right) {
    return fdn_i32_checked((int64_t)left - (int64_t)right);
}

int32_t fdn_i32_multiply(int32_t left, int32_t right) {
    return fdn_i32_checked((int64_t)left * (int64_t)right);
}

int32_t fdn_i32_divide(int32_t left, int32_t right) {
    if (right == 0) {
        fdn_arithmetic_panic("division by zero");
    }
    return fdn_i32_checked((int64_t)left / (int64_t)right);
}

int32_t fdn_i32_remainder(int32_t left, int32_t right) {
    if (right == 0) {
        fdn_arithmetic_panic("remainder by zero");
    }
    return fdn_i32_checked((int64_t)left % (int64_t)right);
}

int32_t fdn_i32_negate(int32_t value) { return fdn_i32_checked(-(int64_t)value); }
