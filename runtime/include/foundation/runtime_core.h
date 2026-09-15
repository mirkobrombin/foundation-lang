#ifndef FOUNDATION_RUNTIME_CORE_H
#define FOUNDATION_RUNTIME_CORE_H

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
              "Foundation f32 requires IEEE 754 binary32");
static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53,
              "Foundation f64 requires IEEE 754 binary64");
#else
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Foundation f32 requires IEEE 754 binary32");
_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53,
               "Foundation f64 requires IEEE 754 binary64");
#endif

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

#ifndef FOUNDATION_FDN_STRING_DEFINED
#define FOUNDATION_FDN_STRING_DEFINED
typedef struct fdn_string {
    const char *data;
    size_t length;
    uint8_t owned;
} fdn_string;
#endif

#ifndef FOUNDATION_FDN_STRING_STATIC_DEFINED
#define FOUNDATION_FDN_STRING_STATIC_DEFINED
static inline fdn_string fdn_string_static(const char *data, size_t length) {
    fdn_string value = {data, length, 0};
    return value;
}
#endif

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
void fdn_abi_string_concat(fdn_string *result, const fdn_string *left,
                           const fdn_string *right);
int fdn_abi_string_equal(const fdn_string *left, const fdn_string *right);
void fdn_abi_println(const fdn_string *value);
void fdn_abi_panic(const fdn_string *message);
size_t fdn_bounds_check(size_t index, size_t length);
void *fdn_alloc(size_t size);
void fdn_dealloc(void *value);
size_t fdn_total_allocations(void);
size_t fdn_total_deallocations(void);
size_t fdn_live_allocations(void);
bool fdn_utf8_valid(const char *value, size_t length);
#ifdef __cplusplus
[[noreturn]] void fdn_panic(fdn_string message);
[[noreturn]] void fdn_panic_cstr(const char *message);
[[noreturn]] void fdn_invalid_enum_tag(void);
#else
_Noreturn void fdn_panic(fdn_string message);
_Noreturn void fdn_panic_cstr(const char *message);
_Noreturn void fdn_invalid_enum_tag(void);
#endif
#define FDN_DECLARE_SIGNED_ARITHMETIC(TYPE, NAME) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_negate(TYPE value)

#define FDN_DECLARE_UNSIGNED_ARITHMETIC(TYPE, NAME) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right)

FDN_DECLARE_SIGNED_ARITHMETIC(int8_t, i8);
FDN_DECLARE_SIGNED_ARITHMETIC(int16_t, i16);
FDN_DECLARE_SIGNED_ARITHMETIC(int32_t, i32);
FDN_DECLARE_SIGNED_ARITHMETIC(int64_t, i64);
FDN_DECLARE_SIGNED_ARITHMETIC(intptr_t, isize);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint8_t, u8);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint16_t, u16);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint32_t, u32);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint64_t, u64);
FDN_DECLARE_UNSIGNED_ARITHMETIC(size_t, usize);

#undef FDN_DECLARE_SIGNED_ARITHMETIC
#undef FDN_DECLARE_UNSIGNED_ARITHMETIC
fdn_string foundation_runtime_string_copy(const fdn_string *value);
uint64_t foundation_runtime_string_byte_length(const fdn_string *value);
bool foundation_runtime_string_contains(const fdn_string *value, const fdn_string *part);
bool foundation_runtime_string_starts_with(const fdn_string *value, const fdn_string *prefix);
bool foundation_runtime_string_ends_with(const fdn_string *value, const fdn_string *suffix);
int32_t foundation_runtime_string_slice(const fdn_string *value, uint64_t start, uint64_t end,
                                        fdn_string *result);
int32_t foundation_runtime_string_byte_at(const fdn_string *value, uint64_t index,
                                          uint64_t *result);
bool foundation_runtime_string_find(const fdn_string *value, const fdn_string *part,
                                    uint64_t *result);
int32_t foundation_runtime_string_compare(const fdn_string *left, const fdn_string *right);
uint64_t foundation_runtime_string_hash_fnv1a(const fdn_string *value);
uint64_t foundation_runtime_string_builder_open(void);
void foundation_runtime_string_builder_append(uint64_t handle, const fdn_string *value);
bool foundation_runtime_string_builder_append_code_point(uint64_t handle, uint64_t value);
fdn_string foundation_runtime_string_builder_finish(uint64_t handle);
void foundation_runtime_string_builder_close(uint64_t handle);
uint64_t foundation_runtime_string_builder_live_handles(void);

#if defined(FOUNDATION_FREESTANDING)
/* Freestanding code has no C library headers. These are the memory primitives every C compiler
   may call, and the only C library symbols a freestanding archive may reference. */
void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
#if !defined(isfinite)
#define isfinite(value) __builtin_isfinite(value)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
