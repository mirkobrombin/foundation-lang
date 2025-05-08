#include "foundation/runtime.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define FDN_THREAD_LOCAL __declspec(thread)
#else
#define FDN_THREAD_LOCAL _Thread_local
#endif

static FDN_THREAD_LOCAL fdn_frame *fdn_current_frame;
static size_t fdn_allocation_count;
static size_t fdn_deallocation_count;
static size_t fdn_live_allocation_count;

static const char *fdn_trace_value(const char *value) {
    return value != NULL ? value : "<unknown>";
}

void fdn_frame_enter(fdn_frame *frame, const char *package_name, const char *function_name,
                     const char *source_file, uint32_t line, uint32_t column) {
    frame->previous = fdn_current_frame;
    frame->package_name = package_name;
    frame->function_name = function_name;
    frame->source_file = source_file;
    frame->line = line;
    frame->column = column;
    frame->native_boundary = 0;
    fdn_current_frame = frame;
}

void fdn_frame_enter_native(fdn_frame *frame, const char *function_name, const char *source_file,
                            uint32_t line, uint32_t column) {
    fdn_frame_enter(frame, NULL, function_name, source_file, line, column);
    frame->native_boundary = 1;
}

void fdn_frame_leave(fdn_frame *frame) {
    if (fdn_current_frame != frame) {
        fdn_panic_cstr("invalid frame chain");
    }
    fdn_current_frame = frame->previous;
}

_Noreturn void fdn_panic(fdn_string message) {
    const fdn_frame *frame;
    fputs("foundation panic: ", stderr);
    if (message.data == NULL) {
        fputs("panic", stderr);
    } else if (message.length != 0) {
        (void)fwrite(message.data, 1, message.length, stderr);
    }
    fputc('\n', stderr);
    for (frame = fdn_current_frame; frame != NULL; frame = frame->previous) {
        if (frame->native_boundary != 0) {
            fprintf(stderr, "  at [native] %s (%s:%u:%u)\n",
                    fdn_trace_value(frame->function_name), fdn_trace_value(frame->source_file),
                    (unsigned int)frame->line,
                    (unsigned int)frame->column);
        } else {
            fprintf(stderr, "  at %s.%s (%s:%u:%u)\n",
                    fdn_trace_value(frame->package_name), fdn_trace_value(frame->function_name),
                    fdn_trace_value(frame->source_file), (unsigned int)frame->line,
                    (unsigned int)frame->column);
        }
    }
    fflush(stderr);
    _Exit(EXIT_FAILURE);
}

_Noreturn void fdn_panic_cstr(const char *message) {
    const char *value = message != NULL ? message : "panic";
    fdn_panic(fdn_string_static(value, strlen(value)));
}

static void fdn_arithmetic_panic(const char *message) {
    char buffer[64];
    (void)snprintf(buffer, sizeof(buffer), "arithmetic error: %s", message);
    fdn_panic_cstr(buffer);
}

static int32_t fdn_i32_checked(int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) {
        fdn_arithmetic_panic("i32 overflow");
    }
    return (int32_t)value;
}

fdn_string fdn_string_move(fdn_string *value) {
    const fdn_string result = *value;
    value->data = NULL;
    value->length = 0;
    value->owned = 0;
    return result;
}

void fdn_string_drop(fdn_string *value) {
    if (value->owned != 0) {
        fdn_dealloc((void *)value->data);
    }
    value->data = NULL;
    value->length = 0;
    value->owned = 0;
}

fdn_string fdn_string_concat(fdn_string left, fdn_string right) {
    fdn_string result;
    char *data;
    if (SIZE_MAX - left.length < right.length) {
        fdn_panic_cstr("string length overflow");
    }
    result.length = left.length + right.length;
    if (result.length == 0) {
        return fdn_string_static("", 0);
    }
    data = fdn_alloc(result.length);
    if (left.length != 0) {
        (void)memcpy(data, left.data, left.length);
    }
    if (right.length != 0) {
        (void)memcpy(data + left.length, right.data, right.length);
    }
    result.data = data;
    result.owned = 1;
    return result;
}

int fdn_string_equal(fdn_string left, fdn_string right) {
    return left.length == right.length &&
           (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

void fdn_println(fdn_string value) {
    if (value.data != NULL && value.length != 0) {
        (void)fwrite(value.data, 1, value.length, stdout);
    }
    fputc('\n', stdout);
}

size_t fdn_bounds_check(int32_t index, size_t length) {
    if (index < 0 || (size_t)index >= length) {
        fdn_panic_cstr("index out of bounds");
    }
    return (size_t)index;
}

void *fdn_alloc(size_t size) {
    void *value = malloc(size == 0 ? 1 : size);
    if (value == NULL) {
        fdn_panic_cstr("allocation failed");
    }
    ++fdn_allocation_count;
    ++fdn_live_allocation_count;
    return value;
}

void fdn_dealloc(void *value) {
    if (value != NULL) {
        ++fdn_deallocation_count;
        --fdn_live_allocation_count;
    }
    free(value);
}

size_t fdn_total_allocations(void) { return fdn_allocation_count; }

size_t fdn_total_deallocations(void) { return fdn_deallocation_count; }

size_t fdn_live_allocations(void) { return fdn_live_allocation_count; }

_Noreturn void fdn_invalid_enum_tag(void) {
    fdn_panic_cstr("invalid enum tag");
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
