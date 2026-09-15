#include "foundation/runtime_core.h"
#include "core_internal.h"

#include <limits.h>

#if defined(FOUNDATION_FREESTANDING)
#include "foundation/freestanding.h"
/* Triples without lock-free pointer-width atomics lower the counters to the __atomic_ support
   routines, which the freestanding archive contract permits. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/* Hosted builds keep the frame chain in thread-local storage. Freestanding builds keep it in the
   first word of the context returned by fdn_hook_context, because bare-metal targets have no TLS
   runtime. */
#if !defined(FOUNDATION_FREESTANDING)
#if defined(_MSC_VER)
#define FDN_THREAD_LOCAL __declspec(thread)
#else
#define FDN_THREAD_LOCAL _Thread_local
#endif

static FDN_THREAD_LOCAL fdn_frame *fdn_current_frame;
#endif

/* The counters are global rather than per context: an owned value may be released on another
   context than the one that allocated it. */
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
static volatile LONG64 fdn_allocation_count;
static volatile LONG64 fdn_deallocation_count;
static volatile LONG64 fdn_live_allocation_count;
#else
static atomic_size_t fdn_allocation_count;
static atomic_size_t fdn_deallocation_count;
static atomic_size_t fdn_live_allocation_count;
#endif
static fdn_handle_count fdn_live_string_builder_count;

#define FDN_ARITHMETIC_PANIC(message) fdn_panic_cstr("arithmetic error: " message)

void fdn_handle_count_add(fdn_handle_count *count) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    if (InterlockedIncrement64(count) <= 0) {
        fdn_panic_cstr("runtime handle count overflow");
    }
#else
    if (atomic_fetch_add_explicit(count, 1, memory_order_relaxed) == FDN_HANDLE_COUNT_MAX) {
        fdn_panic_cstr("runtime handle count overflow");
    }
#endif
}

void fdn_handle_count_remove(fdn_handle_count *count) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    if (InterlockedDecrement64(count) < 0) {
        fdn_panic_cstr("runtime handle count underflow");
    }
#else
    if (atomic_fetch_sub_explicit(count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("runtime handle count underflow");
    }
#endif
}

uint64_t fdn_handle_count_read(fdn_handle_count *count) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    return (uint64_t)InterlockedCompareExchange64(count, 0, 0);
#else
    return atomic_load_explicit(count, memory_order_relaxed);
#endif
}

#if !defined(FOUNDATION_FREESTANDING)
static const char *fdn_trace_value(const char *value) {
    return value != NULL ? value : "<unknown>";
}
#endif

bool fdn_utf8_valid(const char *value, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const unsigned char first = (unsigned char)value[offset];
        if (first <= 0x7f) {
            ++offset;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            if (offset + 1 >= length || (unsigned char)value[offset + 1] < 0x80 ||
                (unsigned char)value[offset + 1] > 0xbf) {
                return 0;
            }
            offset += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            unsigned char second;
            unsigned char third;
            if (offset + 2 >= length) {
                return 0;
            }
            second = (unsigned char)value[offset + 1];
            third = (unsigned char)value[offset + 2];
            if ((first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed && (second < 0x80 || second > 0xbf)) ||
                third < 0x80 || third > 0xbf) {
                return 0;
            }
            offset += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            unsigned char second;
            unsigned char third;
            unsigned char fourth;
            if (offset + 3 >= length) {
                return 0;
            }
            second = (unsigned char)value[offset + 1];
            third = (unsigned char)value[offset + 2];
            fourth = (unsigned char)value[offset + 3];
            if ((first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 && (second < 0x80 || second > 0xbf)) ||
                third < 0x80 || third > 0xbf || fourth < 0x80 || fourth > 0xbf) {
                return 0;
            }
            offset += 4;
            continue;
        }
        return 0;
    }
    return 1;
}

#if defined(FOUNDATION_FREESTANDING)
void fdn_context_init(fdn_context *context) {
    size_t index;
    for (index = 0; index < sizeof(context->fdn_reserved) / sizeof(context->fdn_reserved[0]);
         ++index) {
        context->fdn_reserved[index] = NULL;
    }
}
#endif

void fdn_frame_enter(fdn_frame *frame, const char *package_name, const char *function_name,
                     const char *source_file, uint32_t line, uint32_t column) {
#if defined(FOUNDATION_FREESTANDING)
    fdn_context *context = fdn_hook_context();
    frame->previous = (fdn_frame *)context->fdn_reserved[0];
#else
    frame->previous = fdn_current_frame;
#endif
    frame->package_name = package_name;
    frame->function_name = function_name;
    frame->source_file = source_file;
    frame->line = line;
    frame->column = column;
    frame->native_boundary = 0;
#if defined(FOUNDATION_FREESTANDING)
    context->fdn_reserved[0] = frame;
#else
    fdn_current_frame = frame;
#endif
}

void fdn_frame_enter_native(fdn_frame *frame, const char *function_name, const char *source_file,
                            uint32_t line, uint32_t column) {
    fdn_frame_enter(frame, NULL, function_name, source_file, line, column);
    frame->native_boundary = 1;
}

void fdn_frame_leave(fdn_frame *frame) {
#if defined(FOUNDATION_FREESTANDING)
    fdn_context *context = fdn_hook_context();
    if ((fdn_frame *)context->fdn_reserved[0] != frame) {
        fdn_panic_cstr("invalid frame chain");
    }
    context->fdn_reserved[0] = frame->previous;
#else
    if (fdn_current_frame != frame) {
        fdn_panic_cstr("invalid frame chain");
    }
    fdn_current_frame = frame->previous;
#endif
}

#if defined(FOUNDATION_FREESTANDING)
_Noreturn void fdn_panic(fdn_string message) {
    const fdn_frame *frame = (const fdn_frame *)fdn_hook_context()->fdn_reserved[0];
    fdn_panic_location location;
    const fdn_panic_location *innermost = NULL;
    if (message.data == NULL) {
        message = fdn_string_static("panic", 5);
    }
    if (frame != NULL) {
        location.package_name = frame->native_boundary != 0 ? NULL : frame->package_name;
        location.function_name = frame->function_name;
        location.source_file = frame->source_file;
        location.line = frame->line;
        location.column = frame->column;
        innermost = &location;
    }
    /* The hook contract forbids returning, but the declaration says so too, and a compiler would
       delete the trap after a direct call. The call goes through a pointer the compiler cannot
       prove noreturn, so a hook that returns reaches the trap instead of the panicked frames. */
    {
        void (*volatile hook)(fdn_string, const fdn_panic_location *) = fdn_hook_panic;
        hook(message, innermost);
    }
    __builtin_trap();
}
#else
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
#endif

_Noreturn void fdn_panic_cstr(const char *message) {
    const char *value = message != NULL ? message : "panic";
#if defined(FOUNDATION_FREESTANDING)
    size_t length = 0;
    while (value[length] != '\0') {
        ++length;
    }
    fdn_panic(fdn_string_static(value, length));
#else
    fdn_panic(fdn_string_static(value, strlen(value)));
#endif
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

void fdn_abi_string_concat(fdn_string *result, const fdn_string *left,
                           const fdn_string *right) {
    *result = fdn_string_concat(*left, *right);
}

int fdn_abi_string_equal(const fdn_string *left, const fdn_string *right) {
    return fdn_string_equal(*left, *right);
}

void fdn_abi_panic(const fdn_string *message) { fdn_panic(*message); }

size_t fdn_bounds_check(size_t index, size_t length) {
    if (index >= length) {
        fdn_panic_cstr("index out of bounds");
    }
    return index;
}

void *fdn_alloc(size_t size) {
#if defined(FOUNDATION_FREESTANDING)
    void *value = fdn_hook_alloc(size == 0 ? 1 : size);
#else
    void *value = malloc(size == 0 ? 1 : size);
#endif
    if (value == NULL) {
        fdn_panic_cstr("allocation failed");
    }
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    if ((uint64_t)InterlockedIncrement64(&fdn_allocation_count) > (uint64_t)SIZE_MAX ||
        (uint64_t)InterlockedIncrement64(&fdn_live_allocation_count) > (uint64_t)SIZE_MAX) {
        fdn_panic_cstr("allocation counter overflow");
    }
#else
    if (atomic_fetch_add_explicit(&fdn_allocation_count, 1, memory_order_relaxed) ==
            SIZE_MAX ||
        atomic_fetch_add_explicit(&fdn_live_allocation_count, 1, memory_order_relaxed) ==
            SIZE_MAX) {
        fdn_panic_cstr("allocation counter overflow");
    }
#endif
    return value;
}

void fdn_dealloc(void *value) {
    if (value != NULL) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
        if ((uint64_t)InterlockedIncrement64(&fdn_deallocation_count) > (uint64_t)SIZE_MAX ||
            InterlockedDecrement64(&fdn_live_allocation_count) < 0) {
            fdn_panic_cstr("allocation counter underflow");
        }
#else
        if (atomic_fetch_add_explicit(&fdn_deallocation_count, 1,
                                      memory_order_relaxed) == SIZE_MAX ||
            atomic_fetch_sub_explicit(&fdn_live_allocation_count, 1,
                                      memory_order_relaxed) == 0) {
            fdn_panic_cstr("allocation counter underflow");
        }
#endif
#if defined(FOUNDATION_FREESTANDING)
        fdn_hook_free(value);
#endif
    }
#if !defined(FOUNDATION_FREESTANDING)
    free(value);
#endif
}

size_t fdn_total_allocations(void) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    return (size_t)InterlockedCompareExchange64(&fdn_allocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_allocation_count, memory_order_relaxed);
#endif
}

size_t fdn_total_deallocations(void) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    return (size_t)InterlockedCompareExchange64(&fdn_deallocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_deallocation_count, memory_order_relaxed);
#endif
}

size_t fdn_live_allocations(void) {
#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
    return (size_t)InterlockedCompareExchange64(&fdn_live_allocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_live_allocation_count, memory_order_relaxed);
#endif
}

_Noreturn void fdn_invalid_enum_tag(void) {
    fdn_panic_cstr("invalid enum tag");
}

#define FDN_DEFINE_SIGNED_ARITHMETIC(TYPE, UNSIGNED, NAME, MINIMUM, MAXIMUM) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right) { \
        if ((right > 0 && left > (TYPE)((MAXIMUM) - right)) || \
            (right < 0 && left < (TYPE)((MINIMUM) - right))) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left + right); \
    } \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right) { \
        if ((right < 0 && left > (TYPE)((MAXIMUM) + right)) || \
            (right > 0 && left < (TYPE)((MINIMUM) + right))) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left - right); \
    } \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right) { \
        if (left == 0 || right == 0) { \
            return 0; \
        } \
        if ((left == -1 && right == (MINIMUM)) || \
            (right == -1 && left == (MINIMUM))) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        if ((left > 0 && right > 0 && left > (TYPE)((MAXIMUM) / right)) || \
            (left > 0 && right < 0 && right < (TYPE)((MINIMUM) / left)) || \
            (left < 0 && right > 0 && left < (TYPE)((MINIMUM) / right)) || \
            (left < 0 && right < 0 && left < (TYPE)((MAXIMUM) / right))) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left * right); \
    } \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right) { \
        if (right == 0) { \
            FDN_ARITHMETIC_PANIC("division by zero"); \
        } \
        if (left == (MINIMUM) && right == -1) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left / right); \
    } \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right) { \
        if (right == 0) { \
            FDN_ARITHMETIC_PANIC("remainder by zero"); \
        } \
        if (left == (MINIMUM) && right == -1) { \
            return 0; \
        } \
        return (TYPE)(left % right); \
    } \
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        UNSIGNED factor; \
        if (right < 0 || (uintmax_t)right >= (uintmax_t)width) { \
            FDN_ARITHMETIC_PANIC("shift count out of range"); \
        } \
        if (right == 0) { \
            return left; \
        } \
        if ((unsigned int)right == width - 1) { \
            if (left == 0) { \
                return 0; \
            } \
            if (left == (TYPE)-1) { \
                return (MINIMUM); \
            } \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        factor = (UNSIGNED)1 << (unsigned int)right; \
        if ((left > 0 && left > (TYPE)((MAXIMUM) / (TYPE)factor)) || \
            (left < 0 && left < (TYPE)((MINIMUM) / (TYPE)factor))) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left * (TYPE)factor); \
    } \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if (right < 0 || (uintmax_t)right >= (uintmax_t)width) { \
            FDN_ARITHMETIC_PANIC("shift count out of range"); \
        } \
        if (left >= 0) { \
            return (TYPE)((UNSIGNED)left >> (unsigned int)right); \
        } \
        { \
            const UNSIGNED magnitude = (UNSIGNED)(-(left + 1)); \
            const TYPE shifted = (TYPE)(magnitude >> (unsigned int)right); \
            return (TYPE)(-shifted - 1); \
        } \
    } \
    TYPE fdn_##NAME##_negate(TYPE value) { \
        if (value == (MINIMUM)) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)-value; \
    }

#define FDN_DEFINE_UNSIGNED_ARITHMETIC(TYPE, NAME, MAXIMUM) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right) { \
        if (right > (TYPE)((MAXIMUM) - left)) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left + right); \
    } \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right) { \
        if (right > left) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left - right); \
    } \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right) { \
        if (left != 0 && right > (TYPE)((MAXIMUM) / left)) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left * right); \
    } \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right) { \
        if (right == 0) { \
            FDN_ARITHMETIC_PANIC("division by zero"); \
        } \
        return (TYPE)(left / right); \
    } \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right) { \
        if (right == 0) { \
            FDN_ARITHMETIC_PANIC("remainder by zero"); \
        } \
        return (TYPE)(left % right); \
    } \
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if ((uintmax_t)right >= (uintmax_t)width) { \
            FDN_ARITHMETIC_PANIC("shift count out of range"); \
        } \
        if (right != 0 && left > (TYPE)((MAXIMUM) >> (unsigned int)right)) { \
            FDN_ARITHMETIC_PANIC(#NAME " overflow"); \
        } \
        return (TYPE)(left << (unsigned int)right); \
    } \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if ((uintmax_t)right >= (uintmax_t)width) { \
            FDN_ARITHMETIC_PANIC("shift count out of range"); \
        } \
        return (TYPE)(left >> (unsigned int)right); \
    }

FDN_DEFINE_SIGNED_ARITHMETIC(int8_t, uint8_t, i8, INT8_MIN, INT8_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int16_t, uint16_t, i16, INT16_MIN, INT16_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int32_t, uint32_t, i32, INT32_MIN, INT32_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int64_t, uint64_t, i64, INT64_MIN, INT64_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(intptr_t, uintptr_t, isize, INTPTR_MIN, INTPTR_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint8_t, u8, UINT8_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint16_t, u16, UINT16_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint32_t, u32, UINT32_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint64_t, u64, UINT64_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(size_t, usize, SIZE_MAX)

#undef FDN_DEFINE_SIGNED_ARITHMETIC
#undef FDN_DEFINE_UNSIGNED_ARITHMETIC

fdn_string foundation_runtime_string_copy(const fdn_string *value) {
    fdn_string result;
    char *data;
    if (value == NULL || (value->data == NULL && value->length != 0)) {
        fdn_panic_cstr("invalid String value");
    }
    if (value->length == 0) {
        return fdn_string_static("", 0);
    }
    data = fdn_alloc(value->length);
    (void)memcpy(data, value->data, value->length);
    result.data = data;
    result.length = value->length;
    result.owned = 1;
    return result;
}

uint64_t foundation_runtime_string_byte_length(const fdn_string *value) {
    if (value == NULL) {
        fdn_panic_cstr("invalid String value");
    }
    return (uint64_t)value->length;
}

bool foundation_runtime_string_contains(const fdn_string *value, const fdn_string *part) {
    size_t offset;
    if (value == NULL || part == NULL) {
        fdn_panic_cstr("invalid String value");
    }
    if (part->length == 0) {
        return true;
    }
    if (part->length > value->length) {
        return false;
    }
    for (offset = 0; offset <= value->length - part->length; ++offset) {
        if (memcmp(value->data + offset, part->data, part->length) == 0) {
            return true;
        }
    }
    return false;
}

bool foundation_runtime_string_starts_with(const fdn_string *value, const fdn_string *prefix) {
    if (value == NULL || prefix == NULL) {
        fdn_panic_cstr("invalid String value");
    }
    return prefix->length <= value->length &&
           (prefix->length == 0 || memcmp(value->data, prefix->data, prefix->length) == 0);
}

bool foundation_runtime_string_ends_with(const fdn_string *value, const fdn_string *suffix) {
    if (value == NULL || suffix == NULL) {
        fdn_panic_cstr("invalid String value");
    }
    return suffix->length <= value->length &&
           (suffix->length == 0 ||
            memcmp(value->data + value->length - suffix->length, suffix->data,
                   suffix->length) == 0);
}

static bool fdn_string_boundary(const fdn_string *value, size_t index) {
    return index == value->length || ((unsigned char)value->data[index] & 0xc0) != 0x80;
}

int32_t foundation_runtime_string_slice(const fdn_string *value, uint64_t start, uint64_t end,
                                        fdn_string *result) {
    size_t native_start;
    size_t native_end;
    if (value == NULL || result == NULL) {
        fdn_panic_cstr("invalid String slice argument");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    if (start > end || start > (uint64_t)value->length || end > (uint64_t)value->length) {
        return 1;
    }
    native_start = (size_t)start;
    native_end = (size_t)end;
    if (!fdn_string_boundary(value, native_start) || !fdn_string_boundary(value, native_end)) {
        return 2;
    }
    if (native_start == native_end) {
        return 0;
    }
    *result = foundation_runtime_string_copy(
        &(fdn_string){value->data + native_start, native_end - native_start, 0});
    return 0;
}

int32_t foundation_runtime_string_byte_at(const fdn_string *value, uint64_t index,
                                          uint64_t *result) {
    if (value == NULL || result == NULL) {
        fdn_panic_cstr("invalid String byte argument");
    }
    *result = 0;
    if (index >= (uint64_t)value->length) {
        return 1;
    }
    *result = (uint64_t)(unsigned char)value->data[(size_t)index];
    return 0;
}

bool foundation_runtime_string_find(const fdn_string *value, const fdn_string *part,
                                    uint64_t *result) {
    size_t offset;
    if (value == NULL || part == NULL || result == NULL) {
        fdn_panic_cstr("invalid String find argument");
    }
    *result = 0;
    if (part->length == 0) {
        return true;
    }
    if (part->length > value->length) {
        return false;
    }
    for (offset = 0; offset <= value->length - part->length; ++offset) {
        if (memcmp(value->data + offset, part->data, part->length) == 0) {
            *result = (uint64_t)offset;
            return true;
        }
    }
    return false;
}

int32_t foundation_runtime_string_compare(const fdn_string *left, const fdn_string *right) {
    size_t length;
    int compared;
    if (left == NULL || right == NULL) {
        fdn_panic_cstr("invalid String value");
    }
    length = left->length < right->length ? left->length : right->length;
    compared = length == 0 ? 0 : memcmp(left->data, right->data, length);
    if (compared < 0) {
        return -1;
    }
    if (compared > 0) {
        return 1;
    }
    if (left->length < right->length) {
        return -1;
    }
    if (left->length > right->length) {
        return 1;
    }
    return 0;
}

uint64_t foundation_runtime_string_hash_fnv1a(const fdn_string *value) {
    uint64_t hash = UINT64_C(14695981039346656037);
    if (value == NULL || (value->data == NULL && value->length != 0)) {
        fdn_panic_cstr("invalid string hash input");
    }
    for (size_t index = 0; index < value->length; ++index) {
        hash ^= (uint8_t)value->data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

typedef struct fdn_string_builder {
    char *data;
    size_t length;
    size_t capacity;
} fdn_string_builder;

static fdn_string_builder *fdn_builder(uint64_t handle) {
    fdn_string_builder *builder = (fdn_string_builder *)(uintptr_t)handle;
    if (builder == NULL) {
        fdn_panic_cstr("string builder is closed");
    }
    return builder;
}

static void fdn_builder_reserve(fdn_string_builder *builder, size_t required) {
    size_t capacity;
    char *data;
    if (required <= builder->capacity) {
        return;
    }
    capacity = builder->capacity == 0 ? 64 : builder->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    data = fdn_alloc(capacity);
    if (builder->length != 0) {
        (void)memcpy(data, builder->data, builder->length);
    }
    fdn_dealloc(builder->data);
    builder->data = data;
    builder->capacity = capacity;
}

uint64_t foundation_runtime_string_builder_open(void) {
    fdn_string_builder *builder = fdn_alloc(sizeof(*builder));
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
    fdn_handle_count_add(&fdn_live_string_builder_count);
    return (uint64_t)(uintptr_t)builder;
}

void foundation_runtime_string_builder_append(uint64_t handle, const fdn_string *value) {
    fdn_string_builder *builder = fdn_builder(handle);
    size_t required;
    if (value == NULL || (value->data == NULL && value->length != 0)) {
        fdn_panic_cstr("invalid String value");
    }
    if (SIZE_MAX - builder->length < value->length) {
        fdn_panic_cstr("string length overflow");
    }
    required = builder->length + value->length;
    fdn_builder_reserve(builder, required);
    if (value->length != 0) {
        (void)memcpy(builder->data + builder->length, value->data, value->length);
    }
    builder->length = required;
}

bool foundation_runtime_string_builder_append_code_point(uint64_t handle, uint64_t value) {
    char bytes[4];
    size_t length;
    if (value <= 0x7f) {
        bytes[0] = (char)value;
        length = 1;
    } else if (value <= 0x7ff) {
        bytes[0] = (char)(0xc0 | (value >> 6));
        bytes[1] = (char)(0x80 | (value & 0x3f));
        length = 2;
    } else if (value >= 0xd800 && value <= 0xdfff) {
        return false;
    } else if (value <= 0xffff) {
        bytes[0] = (char)(0xe0 | (value >> 12));
        bytes[1] = (char)(0x80 | ((value >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (value & 0x3f));
        length = 3;
    } else if (value <= 0x10ffff) {
        bytes[0] = (char)(0xf0 | (value >> 18));
        bytes[1] = (char)(0x80 | ((value >> 12) & 0x3f));
        bytes[2] = (char)(0x80 | ((value >> 6) & 0x3f));
        bytes[3] = (char)(0x80 | (value & 0x3f));
        length = 4;
    } else {
        return false;
    }
    foundation_runtime_string_builder_append(handle, &(fdn_string){bytes, length, 0});
    return true;
}

fdn_string foundation_runtime_string_builder_finish(uint64_t handle) {
    fdn_string_builder *builder = fdn_builder(handle);
    fdn_string result;
    if (!fdn_utf8_valid(builder->data, builder->length)) {
        fdn_panic_cstr("string builder produced invalid UTF-8");
    }
    if (builder->length == 0) {
        result = fdn_string_static("", 0);
        fdn_dealloc(builder->data);
    } else {
        result.data = builder->data;
        result.length = builder->length;
        result.owned = 1;
    }
    fdn_dealloc(builder);
    fdn_handle_count_remove(&fdn_live_string_builder_count);
    return result;
}

void foundation_runtime_string_builder_close(uint64_t handle) {
    fdn_string_builder *builder = (fdn_string_builder *)(uintptr_t)handle;
    if (builder == NULL) {
        return;
    }
    fdn_dealloc(builder->data);
    fdn_dealloc(builder);
    fdn_handle_count_remove(&fdn_live_string_builder_count);
}

uint64_t foundation_runtime_string_builder_live_handles(void) {
    return fdn_handle_count_read(&fdn_live_string_builder_count);
}
