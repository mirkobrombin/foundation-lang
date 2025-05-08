#ifndef FOUNDATION_RUNTIME_H
#define FOUNDATION_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fdn_frame {
    struct fdn_frame *previous;
    const char *package_name;
    const char *function_name;
    const char *source_file;
    uint32_t line;
    uint32_t column;
    uint8_t native_boundary;
} fdn_frame;

typedef struct fdn_string {
    const char *data;
    size_t length;
    uint8_t owned;
} fdn_string;

static inline fdn_string fdn_string_static(const char *data, size_t length) {
    fdn_string value = {data, length, 0};
    return value;
}

void fdn_frame_enter(fdn_frame *frame, const char *package_name, const char *function_name,
                     const char *source_file, uint32_t line, uint32_t column);
void fdn_frame_enter_native(fdn_frame *frame, const char *function_name, const char *source_file,
                            uint32_t line, uint32_t column);
void fdn_frame_leave(fdn_frame *frame);
static inline void fdn_frame_location(fdn_frame *frame, uint32_t line, uint32_t column) {
    frame->line = line;
    frame->column = column;
}

fdn_string fdn_string_move(fdn_string *value);
void fdn_string_drop(fdn_string *value);
fdn_string fdn_string_concat(fdn_string left, fdn_string right);
int fdn_string_equal(fdn_string left, fdn_string right);
void fdn_println(fdn_string value);
size_t fdn_bounds_check(int32_t index, size_t length);
void *fdn_alloc(size_t size);
void fdn_dealloc(void *value);
size_t fdn_total_allocations(void);
size_t fdn_total_deallocations(void);
size_t fdn_live_allocations(void);
#ifdef __cplusplus
[[noreturn]] void fdn_panic(fdn_string message);
[[noreturn]] void fdn_panic_cstr(const char *message);
[[noreturn]] void fdn_invalid_enum_tag(void);
#else
_Noreturn void fdn_panic(fdn_string message);
_Noreturn void fdn_panic_cstr(const char *message);
_Noreturn void fdn_invalid_enum_tag(void);
#endif
int32_t fdn_i32_add(int32_t left, int32_t right);
int32_t fdn_i32_subtract(int32_t left, int32_t right);
int32_t fdn_i32_multiply(int32_t left, int32_t right);
int32_t fdn_i32_divide(int32_t left, int32_t right);
int32_t fdn_i32_remainder(int32_t left, int32_t right);
int32_t fdn_i32_negate(int32_t value);

#ifdef __cplusplus
}
#endif

#endif
