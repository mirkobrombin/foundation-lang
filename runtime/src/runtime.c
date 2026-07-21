#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "runtime handles require a 64-bit carrier");

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <share.h>
#include <windows.h>
#include <bcrypt.h>
#include <wchar.h>
static SRWLOCK fdn_stdout_lock = SRWLOCK_INIT;
static SRWLOCK fdn_uuid_lock = SRWLOCK_INIT;
#else
#include <dirent.h>
#include <stdatomic.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
static atomic_flag fdn_uuid_lock = ATOMIC_FLAG_INIT;
#endif

#if defined(_MSC_VER)
#define FDN_THREAD_LOCAL __declspec(thread)
#else
#define FDN_THREAD_LOCAL _Thread_local
#endif

static FDN_THREAD_LOCAL fdn_frame *fdn_current_frame;
#if defined(_WIN32)
static volatile LONG64 fdn_allocation_count;
static volatile LONG64 fdn_deallocation_count;
static volatile LONG64 fdn_live_allocation_count;
static volatile LONG64 fdn_live_file_count;
static volatile LONG64 fdn_live_directory_count;
static volatile LONG64 fdn_live_string_builder_count;
#else
static atomic_size_t fdn_allocation_count;
static atomic_size_t fdn_deallocation_count;
static atomic_size_t fdn_live_allocation_count;
static atomic_uint_fast64_t fdn_live_file_count;
static atomic_uint_fast64_t fdn_live_directory_count;
static atomic_uint_fast64_t fdn_live_string_builder_count;
#endif

static void fdn_handle_count_add(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    if (InterlockedIncrement64(count) <= 0) {
        fdn_panic_cstr("runtime handle count overflow");
    }
#else
    if (atomic_fetch_add_explicit(count, 1, memory_order_relaxed) == UINT64_MAX) {
        fdn_panic_cstr("runtime handle count overflow");
    }
#endif
}

static void fdn_handle_count_remove(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    if (InterlockedDecrement64(count) < 0) {
        fdn_panic_cstr("runtime handle count underflow");
    }
#else
    if (atomic_fetch_sub_explicit(count, 1, memory_order_relaxed) == 0) {
        fdn_panic_cstr("runtime handle count underflow");
    }
#endif
}

static uint64_t fdn_handle_count_read(
#if defined(_WIN32)
    volatile LONG64 *count
#else
    atomic_uint_fast64_t *count
#endif
) {
#if defined(_WIN32)
    return (uint64_t)InterlockedCompareExchange64(count, 0, 0);
#else
    return atomic_load_explicit(count, memory_order_relaxed);
#endif
}

static const char *fdn_trace_value(const char *value) {
    return value != NULL ? value : "<unknown>";
}

void fdn_retry_wait(uint32_t retry_index) {
    const uint32_t shift = retry_index > 10 ? 10 : retry_index;
    const uint32_t milliseconds = UINT32_C(1) << shift;
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            fdn_panic_cstr("retry wait failed");
        }
    }
#endif
}

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

static int fdn_valid_env_name(const fdn_string *name) {
    size_t offset;
    if (name == NULL || name->data == NULL || name->length == 0) {
        return 0;
    }
    for (offset = 0; offset < name->length; ++offset) {
        if (name->data[offset] == '\0' || name->data[offset] == '=') {
            return 0;
        }
    }
    return 1;
}

#if !defined(_WIN32)
static fdn_string fdn_string_copy(const char *value, size_t length) {
    fdn_string result;
    char *copy;
    if (length == 0) {
        return fdn_string_static("", 0);
    }
    copy = fdn_alloc(length);
    (void)memcpy(copy, value, length);
    result.data = copy;
    result.length = length;
    result.owned = 1;
    return result;
}
#endif

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
#if defined(_WIN32)
    AcquireSRWLockExclusive(&fdn_stdout_lock);
#else
    flockfile(stdout);
#endif
    if (value.data != NULL && value.length != 0) {
        (void)fwrite(value.data, 1, value.length, stdout);
    }
    fputc('\n', stdout);
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&fdn_stdout_lock);
#else
    funlockfile(stdout);
#endif
}

size_t fdn_bounds_check(size_t index, size_t length) {
    if (index >= length) {
        fdn_panic_cstr("index out of bounds");
    }
    return index;
}

void *fdn_alloc(size_t size) {
    void *value = malloc(size == 0 ? 1 : size);
    if (value == NULL) {
        fdn_panic_cstr("allocation failed");
    }
#if defined(_WIN32)
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
#if defined(_WIN32)
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
    }
    free(value);
}

size_t fdn_total_allocations(void) {
#if defined(_WIN32)
    return (size_t)InterlockedCompareExchange64(&fdn_allocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_allocation_count, memory_order_relaxed);
#endif
}

size_t fdn_total_deallocations(void) {
#if defined(_WIN32)
    return (size_t)InterlockedCompareExchange64(&fdn_deallocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_deallocation_count, memory_order_relaxed);
#endif
}

size_t fdn_live_allocations(void) {
#if defined(_WIN32)
    return (size_t)InterlockedCompareExchange64(&fdn_live_allocation_count, 0, 0);
#else
    return atomic_load_explicit(&fdn_live_allocation_count, memory_order_relaxed);
#endif
}

_Noreturn void fdn_invalid_enum_tag(void) {
    fdn_panic_cstr("invalid enum tag");
}

#define FDN_DEFINE_SIGNED_ARITHMETIC(TYPE, NAME, MINIMUM, MAXIMUM) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right) { \
        if ((right > 0 && left > (TYPE)((MAXIMUM) - right)) || \
            (right < 0 && left < (TYPE)((MINIMUM) - right))) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left + right); \
    } \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right) { \
        if ((right < 0 && left > (TYPE)((MAXIMUM) + right)) || \
            (right > 0 && left < (TYPE)((MINIMUM) + right))) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left - right); \
    } \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right) { \
        if (left == 0 || right == 0) { \
            return 0; \
        } \
        if ((left == -1 && right == (MINIMUM)) || \
            (right == -1 && left == (MINIMUM))) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        if ((left > 0 && right > 0 && left > (TYPE)((MAXIMUM) / right)) || \
            (left > 0 && right < 0 && right < (TYPE)((MINIMUM) / left)) || \
            (left < 0 && right > 0 && left < (TYPE)((MINIMUM) / right)) || \
            (left < 0 && right < 0 && left < (TYPE)((MAXIMUM) / right))) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left * right); \
    } \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right) { \
        if (right == 0) { \
            fdn_arithmetic_panic("division by zero"); \
        } \
        if (left == (MINIMUM) && right == -1) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left / right); \
    } \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right) { \
        if (right == 0) { \
            fdn_arithmetic_panic("remainder by zero"); \
        } \
        if (left == (MINIMUM) && right == -1) { \
            return 0; \
        } \
        return (TYPE)(left % right); \
    } \
    TYPE fdn_##NAME##_negate(TYPE value) { \
        if (value == (MINIMUM)) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)-value; \
    }

#define FDN_DEFINE_UNSIGNED_ARITHMETIC(TYPE, NAME, MAXIMUM) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right) { \
        if (right > (TYPE)((MAXIMUM) - left)) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left + right); \
    } \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right) { \
        if (right > left) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left - right); \
    } \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right) { \
        if (left != 0 && right > (TYPE)((MAXIMUM) / left)) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left * right); \
    } \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right) { \
        if (right == 0) { \
            fdn_arithmetic_panic("division by zero"); \
        } \
        return (TYPE)(left / right); \
    } \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right) { \
        if (right == 0) { \
            fdn_arithmetic_panic("remainder by zero"); \
        } \
        return (TYPE)(left % right); \
    }

FDN_DEFINE_SIGNED_ARITHMETIC(int8_t, i8, INT8_MIN, INT8_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int16_t, i16, INT16_MIN, INT16_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int32_t, i32, INT32_MIN, INT32_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(int64_t, i64, INT64_MIN, INT64_MAX)
FDN_DEFINE_SIGNED_ARITHMETIC(intptr_t, isize, INTPTR_MIN, INTPTR_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint8_t, u8, UINT8_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint16_t, u16, UINT16_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint32_t, u32, UINT32_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(uint64_t, u64, UINT64_MAX)
FDN_DEFINE_UNSIGNED_ARITHMETIC(size_t, usize, SIZE_MAX)

#undef FDN_DEFINE_SIGNED_ARITHMETIC
#undef FDN_DEFINE_UNSIGNED_ARITHMETIC

int32_t foundation_runtime_env_read(const fdn_string *name, fdn_string *value) {
    if (value == NULL) {
        fdn_panic_cstr("environment output is null");
    }
    fdn_string_drop(value);
    *value = fdn_string_static("", 0);
    if (!fdn_valid_env_name(name) || !fdn_utf8_valid(name->data, name->length)) {
        return 2;
    }

#if defined(_WIN32)
    {
        wchar_t *wide_name;
        wchar_t *wide_value;
        char *utf8_value;
        int wide_name_length;
        DWORD required;
        DWORD copied;
        int utf8_length;

        if (name->length > (size_t)INT_MAX) {
            return 2;
        }
        wide_name_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name->data,
                                                (int)name->length, NULL, 0);
        if (wide_name_length == 0) {
            return 2;
        }
        wide_name = fdn_alloc(((size_t)wide_name_length + 1) * sizeof(*wide_name));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name->data, (int)name->length,
                                wide_name, wide_name_length) != wide_name_length) {
            fdn_dealloc(wide_name);
            return 2;
        }
        wide_name[wide_name_length] = L'\0';

        SetLastError(ERROR_SUCCESS);
        required = GetEnvironmentVariableW(wide_name, NULL, 0);
        if (required == 0) {
            const DWORD error = GetLastError();
            fdn_dealloc(wide_name);
            return error == ERROR_ENVVAR_NOT_FOUND ? 0 : error == ERROR_SUCCESS ? 1 : 4;
        }
        wide_value = fdn_alloc((size_t)required * sizeof(*wide_value));
        copied = GetEnvironmentVariableW(wide_name, wide_value, required);
        fdn_dealloc(wide_name);
        if (copied == 0 || copied >= required || copied > (DWORD)INT_MAX) {
            fdn_dealloc(wide_value);
            return 4;
        }
        utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_value, (int)copied,
                                          NULL, 0, NULL, NULL);
        if (utf8_length == 0) {
            fdn_dealloc(wide_value);
            return 3;
        }
        utf8_value = fdn_alloc((size_t)utf8_length);
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_value, (int)copied,
                                utf8_value, utf8_length, NULL, NULL) != utf8_length) {
            fdn_dealloc(utf8_value);
            fdn_dealloc(wide_value);
            return 3;
        }
        fdn_dealloc(wide_value);
        value->data = utf8_value;
        value->length = (size_t)utf8_length;
        value->owned = 1;
        return 1;
    }
#else
    {
        char *native_name;
        const char *native_value;
        size_t length;
        if (name->length == SIZE_MAX) {
            return 2;
        }
        native_name = fdn_alloc(name->length + 1);
        (void)memcpy(native_name, name->data, name->length);
        native_name[name->length] = '\0';
        native_value = getenv(native_name);
        fdn_dealloc(native_name);
        if (native_value == NULL) {
            return 0;
        }
        length = strlen(native_value);
        if (!fdn_utf8_valid(native_value, length)) {
            return 3;
        }
        *value = fdn_string_copy(native_value, length);
        return 1;
    }
#endif
}

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

static fdn_string fdn_format_result(const char *type_name, char *buffer, size_t capacity,
                                    int length) {
    if (length < 0 || (size_t)length >= capacity) {
        char message[64];
        const int message_length =
            snprintf(message, sizeof(message), "%s formatting failed", type_name);
        if (message_length < 0 || (size_t)message_length >= sizeof(message)) {
            fdn_panic_cstr("scalar formatting failed");
        }
        fdn_panic_cstr(message);
    }
    return foundation_runtime_string_copy(&(fdn_string){buffer, (size_t)length, 0});
}

static int fdn_normalize_decimal_point(char *buffer, size_t capacity, int length) {
    const struct lconv *locale = localeconv();
    const char *point = locale == NULL ? NULL : locale->decimal_point;
    if (point == NULL || point[0] == '\0' || strcmp(point, ".") == 0) {
        return length;
    }
    char *position = strstr(buffer, point);
    if (position == NULL) {
        return length;
    }
    const size_t point_length = strlen(point);
    const size_t prefix_length = (size_t)(position - buffer);
    const size_t suffix_length = (size_t)length - prefix_length - point_length;
    if (prefix_length + 1 + suffix_length >= capacity) {
        fdn_panic_cstr("floating-point formatting failed");
    }
    position[0] = '.';
    memmove(position + 1, position + point_length, suffix_length + 1);
    return (int)(prefix_length + 1 + suffix_length);
}

fdn_string foundation_runtime_format_bool(bool value) {
    const char *text = value ? "true" : "false";
    return foundation_runtime_string_copy(
        &(fdn_string){text, value ? (size_t)4 : (size_t)5, 0});
}

#define FDN_DEFINE_SIGNED_FORMAT(TYPE, NAME, FORMAT)                                      \
    fdn_string foundation_runtime_format_##NAME(TYPE value) {                            \
        char buffer[32];                                                                  \
        const int length = snprintf(buffer, sizeof(buffer), "%" FORMAT, value);          \
        return fdn_format_result(#NAME, buffer, sizeof(buffer), length);                  \
    }

#define FDN_DEFINE_UNSIGNED_FORMAT(TYPE, NAME, FORMAT)                                    \
    fdn_string foundation_runtime_format_##NAME(TYPE value) {                            \
        char buffer[32];                                                                  \
        const int length = snprintf(buffer, sizeof(buffer), "%" FORMAT, value);          \
        return fdn_format_result(#NAME, buffer, sizeof(buffer), length);                  \
    }

FDN_DEFINE_SIGNED_FORMAT(int8_t, i8, PRId8)
FDN_DEFINE_SIGNED_FORMAT(int16_t, i16, PRId16)
FDN_DEFINE_SIGNED_FORMAT(int32_t, i32, PRId32)
FDN_DEFINE_SIGNED_FORMAT(int64_t, i64, PRId64)
FDN_DEFINE_SIGNED_FORMAT(intptr_t, isize, PRIdPTR)
FDN_DEFINE_UNSIGNED_FORMAT(uint8_t, u8, PRIu8)
FDN_DEFINE_UNSIGNED_FORMAT(uint16_t, u16, PRIu16)
FDN_DEFINE_UNSIGNED_FORMAT(uint32_t, u32, PRIu32)
FDN_DEFINE_UNSIGNED_FORMAT(uint64_t, u64, PRIu64)

#undef FDN_DEFINE_SIGNED_FORMAT
#undef FDN_DEFINE_UNSIGNED_FORMAT

fdn_string foundation_runtime_format_usize(size_t value) {
    char buffer[32];
    const int length = snprintf(buffer, sizeof(buffer), "%" PRIuPTR, (uintptr_t)value);
    return fdn_format_result("usize", buffer, sizeof(buffer), length);
}

fdn_string foundation_runtime_format_f32(float value) {
    if (isnan(value)) {
        return foundation_runtime_string_copy(&(fdn_string){"NaN", 3, 0});
    }
    if (isinf(value)) {
        const char *text = signbit(value) ? "-Infinity" : "Infinity";
        return foundation_runtime_string_copy(
            &(fdn_string){text, signbit(value) ? (size_t)9 : (size_t)8, 0});
    }
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%.9g", (double)value);
    if (length >= 0 && (size_t)length < sizeof(buffer)) {
        length = fdn_normalize_decimal_point(buffer, sizeof(buffer), length);
    }
    return fdn_format_result("f32", buffer, sizeof(buffer), length);
}

fdn_string foundation_runtime_format_f64(double value) {
    if (isnan(value)) {
        return foundation_runtime_string_copy(&(fdn_string){"NaN", 3, 0});
    }
    if (isinf(value)) {
        const char *text = signbit(value) ? "-Infinity" : "Infinity";
        return foundation_runtime_string_copy(
            &(fdn_string){text, signbit(value) ? (size_t)9 : (size_t)8, 0});
    }
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (length >= 0 && (size_t)length < sizeof(buffer)) {
        length = fdn_normalize_decimal_point(buffer, sizeof(buffer), length);
    }
    return fdn_format_result("f64", buffer, sizeof(buffer), length);
}

uint64_t foundation_runtime_time_unix_seconds(void) {
    const time_t value = time(NULL);
    if (value == (time_t)-1) {
        fdn_panic_cstr("system time is unavailable");
    }
    return (uint64_t)value;
}

uint64_t foundation_runtime_time_monotonic_nanoseconds(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64() * UINT64_C(1000000);
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        fdn_panic_cstr("monotonic clock failed");
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
#endif
}

int32_t foundation_runtime_time_format_utc(uint64_t unix_seconds, fdn_string *result) {
    struct tm calendar;
    char buffer[21];
    int length;
    if (result == NULL) {
        return 1;
    }
#if defined(_WIN32)
    {
        __time64_t native;
        if (unix_seconds > (uint64_t)INT64_MAX) {
            return 1;
        }
        native = (__time64_t)unix_seconds;
        if (_gmtime64_s(&calendar, &native) != 0) {
            return 1;
        }
    }
#else
    {
        time_t native = (time_t)unix_seconds;
        if (native < (time_t)0 || (uint64_t)native != unix_seconds ||
            gmtime_r(&native, &calendar) == NULL) {
            return 1;
        }
    }
#endif
    if (calendar.tm_year < -1900 || calendar.tm_year > 8099) {
        return 1;
    }
    length = snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                      calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday,
                      calendar.tm_hour, calendar.tm_min, calendar.tm_sec);
    if (length != 20) {
        return 1;
    }
    *result = foundation_runtime_string_copy(&(fdn_string){buffer, 20, 0});
    return 0;
}

static uint64_t fdn_uuid_unix_milliseconds(void) {
#if defined(_WIN32)
    FILETIME value;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&value);
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return (ticks.QuadPart - UINT64_C(116444736000000000)) / UINT64_C(10000);
#else
    struct timespec value;
    if (clock_gettime(CLOCK_REALTIME, &value) != 0) {
        fdn_panic_cstr("UUID clock failed");
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000) +
           (uint64_t)value.tv_nsec / UINT64_C(1000000);
#endif
}

static void fdn_uuid_random(void *buffer, size_t length) {
#if defined(_WIN32)
    if (BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)length,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fdn_panic_cstr("UUID entropy failed");
    }
#elif defined(__APPLE__)
    arc4random_buf(buffer, length);
#elif defined(__linux__)
    size_t offset = 0;
    while (offset < length) {
        const ssize_t read = getrandom((unsigned char *)buffer + offset, length - offset, 0);
        if (read > 0) {
            offset += (size_t)read;
        } else if (read < 0 && errno == EINTR) {
            continue;
        } else {
            fdn_panic_cstr("UUID entropy failed");
        }
    }
#else
    FILE *source = fopen("/dev/urandom", "rb");
    if (source == NULL) {
        fdn_panic_cstr("UUID entropy failed");
    }
    if (fread(buffer, 1, length, source) != length) {
        (void)fclose(source);
        fdn_panic_cstr("UUID entropy failed");
    }
    if (fclose(source) != 0) {
        fdn_panic_cstr("UUID entropy failed");
    }
#endif
}

void foundation_runtime_uuid_v4(uint64_t *high, uint64_t *low) {
    uint64_t words[2];
    if (high == NULL || low == NULL) {
        fdn_panic_cstr("UUID output is null");
    }
    fdn_uuid_random(words, sizeof(words));
    *high = (words[0] & ~UINT64_C(0xf000)) | UINT64_C(0x4000);
    *low = (words[1] & UINT64_C(0x3fffffffffffffff)) |
           UINT64_C(0x8000000000000000);
}

void foundation_runtime_uuid_v7(uint64_t *high, uint64_t *low) {
    static bool initialized;
    static uint64_t last_high;
    static uint64_t last_low;
    uint64_t words[2];
    const uint64_t milliseconds = fdn_uuid_unix_milliseconds();
    bool exhausted = false;

    if (high == NULL || low == NULL) {
        fdn_panic_cstr("UUID output is null");
    }
    if (milliseconds > UINT64_C(0xffffffffffff)) {
        fdn_panic_cstr("UUID v7 timestamp is out of range");
    }
    fdn_uuid_random(words, sizeof(words));
#if defined(_WIN32)
    AcquireSRWLockExclusive(&fdn_uuid_lock);
#else
    while (atomic_flag_test_and_set_explicit(&fdn_uuid_lock, memory_order_acquire)) {
    }
#endif
    if (!initialized || milliseconds > (last_high >> 16U)) {
        *high = (milliseconds << 16U) | UINT64_C(0x7000) |
                (words[0] & UINT64_C(0x0fff));
        *low = (words[1] & UINT64_C(0x3fffffffffffffff)) |
               UINT64_C(0x8000000000000000);
    } else {
        *high = last_high;
        *low = last_low;
        if ((*low & UINT64_C(0x3fffffffffffffff)) !=
            UINT64_C(0x3fffffffffffffff)) {
            *low += 1;
        } else if ((*high & UINT64_C(0x0fff)) != UINT64_C(0x0fff)) {
            *high += 1;
            *low = UINT64_C(0x8000000000000000);
        } else {
            exhausted = true;
        }
    }
    if (!exhausted) {
        initialized = true;
        last_high = *high;
        last_low = *low;
    }
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&fdn_uuid_lock);
#else
    atomic_flag_clear_explicit(&fdn_uuid_lock, memory_order_release);
#endif
    if (exhausted) {
        fdn_panic_cstr("UUID v7 sequence exhausted");
    }
}

static int32_t fdn_fs_status(int error) {
    if (error == ENOENT) {
        return 1;
    }
    if (error == EACCES) {
        return 2;
    }
    if (error == EINVAL || error == ENAMETOOLONG) {
        return 3;
    }
    return 4;
}

static int fdn_valid_path(const fdn_string *path) {
    size_t offset;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        !fdn_utf8_valid(path->data, path->length)) {
        return 0;
    }
    for (offset = 0; offset < path->length; ++offset) {
        if (path->data[offset] == '\0') {
            return 0;
        }
    }
    return 1;
}

#if defined(_WIN32)
static wchar_t *fdn_windows_path(const fdn_string *path) {
    wchar_t *result;
    int length;
    if (!fdn_valid_path(path) || path->length > (size_t)INT_MAX) {
        return NULL;
    }
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data,
                                 (int)path->length, NULL, 0);
    if (length == 0) {
        return NULL;
    }
    result = fdn_alloc(((size_t)length + 1) * sizeof(*result));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path->data, (int)path->length,
                            result, length) != length) {
        fdn_dealloc(result);
        return NULL;
    }
    result[length] = L'\0';
    return result;
}
#else
static char *fdn_native_path(const fdn_string *path) {
    char *result;
    if (!fdn_valid_path(path) || path->length == SIZE_MAX) {
        return NULL;
    }
    result = fdn_alloc(path->length + 1);
    (void)memcpy(result, path->data, path->length);
    result[path->length] = '\0';
    return result;
}
#endif

static FILE *fdn_fs_open_file(const fdn_string *path, int32_t *status) {
    FILE *file;
    if (status == NULL) {
        fdn_panic_cstr("file status output is null");
    }
    *status = 3;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_windows_path(path);
        if (native_path == NULL) {
            return NULL;
        }
        file = _wfsopen(native_path, L"rb", _SH_DENYNO);
        fdn_dealloc(native_path);
    }
#else
    {
        char *native_path = fdn_native_path(path);
        if (native_path == NULL) {
            return NULL;
        }
        file = fopen(native_path, "rb");
        fdn_dealloc(native_path);
    }
#endif
    if (file == NULL) {
        *status = fdn_fs_status(errno);
        return NULL;
    }
    *status = 0;
    return file;
}

int32_t foundation_runtime_fs_open_lines(const fdn_string *path, uint64_t *handle) {
    FILE *file;
    int32_t status;
    if (handle == NULL) {
        fdn_panic_cstr("file handle output is null");
    }
    *handle = 0;
    file = fdn_fs_open_file(path, &status);
    if (file == NULL) {
        return status;
    }
    *handle = (uint64_t)(uintptr_t)file;
    fdn_handle_count_add(&fdn_live_file_count);
    return 0;
}

int32_t foundation_runtime_fs_next_line_limited(uint64_t handle, uint64_t max_length,
                                                fdn_string *line) {
    FILE *file = (FILE *)(uintptr_t)handle;
    char *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const size_t limit = max_length > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)max_length;
    int byte;
    if (file == NULL || line == NULL) {
        return 4;
    }
    fdn_string_drop(line);
    *line = fdn_string_static("", 0);
    while ((byte = fgetc(file)) != EOF) {
        char *grown;
        size_t next_capacity;
        if (byte == '\n') {
            break;
        }
        if (length >= limit) {
            while ((byte = fgetc(file)) != EOF && byte != '\n') {
            }
            fdn_dealloc(data);
            return byte == EOF && ferror(file) != 0 ? 4 : 5;
        }
        if (length == capacity) {
            next_capacity = capacity == 0 ? 256 : capacity * 2;
            if (next_capacity < capacity) {
                fdn_dealloc(data);
                fdn_panic_cstr("line length overflow");
            }
            if (next_capacity > limit) {
                next_capacity = limit;
            }
            grown = fdn_alloc(next_capacity);
            if (length != 0) {
                (void)memcpy(grown, data, length);
            }
            fdn_dealloc(data);
            data = grown;
            capacity = next_capacity;
        }
        data[length++] = (char)byte;
    }
    if (byte == EOF && ferror(file) != 0) {
        fdn_dealloc(data);
        return 4;
    }
    if (byte == EOF && length == 0) {
        return 0;
    }
    if (length != 0 && data[length - 1] == '\r') {
        --length;
    }
    if (!fdn_utf8_valid(data, length)) {
        fdn_dealloc(data);
        return 3;
    }
    if (length == 0) {
        fdn_dealloc(data);
        return 1;
    }
    line->data = data;
    line->length = length;
    line->owned = 1;
    return 1;
}

int32_t foundation_runtime_fs_next_line(uint64_t handle, fdn_string *line) {
    return foundation_runtime_fs_next_line_limited(handle, UINT64_MAX, line);
}

int32_t foundation_runtime_fs_close(uint64_t handle) {
    FILE *file = (FILE *)(uintptr_t)handle;
    int status;
    if (file == NULL) {
        return 0;
    }
    status = fclose(file);
    fdn_handle_count_remove(&fdn_live_file_count);
    return status == 0 ? 0 : 4;
}

int32_t foundation_runtime_fs_size(const fdn_string *path, uint64_t *size) {
    if (size == NULL) {
        fdn_panic_cstr("file size output is null");
    }
    *size = 0;
#if defined(_WIN32)
    {
        struct _stat64 info;
        wchar_t *native_path = fdn_windows_path(path);
        int status;
        if (native_path == NULL) {
            return 3;
        }
        status = _wstat64(native_path, &info);
        fdn_dealloc(native_path);
        if (status != 0) {
            return fdn_fs_status(errno);
        }
        if (info.st_size < 0) {
            return 4;
        }
        *size = (uint64_t)info.st_size;
    }
#else
    {
        struct stat info;
        char *native_path = fdn_native_path(path);
        int status;
        if (native_path == NULL) {
            return 3;
        }
        status = stat(native_path, &info);
        fdn_dealloc(native_path);
        if (status != 0) {
            return fdn_fs_status(errno);
        }
        if (info.st_size < 0) {
            return 4;
        }
        *size = (uint64_t)info.st_size;
    }
#endif
    return 0;
}

uint64_t foundation_runtime_fs_live_handles(void) {
    return fdn_handle_count_read(&fdn_live_file_count);
}

#if defined(_WIN32)
typedef struct fdn_directory_handle {
    HANDLE search;
    WIN32_FIND_DATAW entry;
    int first;
} fdn_directory_handle;

static int32_t fdn_windows_fs_status(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return 1;
    }
    if (error == ERROR_ACCESS_DENIED) {
        return 2;
    }
    if (error == ERROR_INVALID_NAME || error == ERROR_BAD_PATHNAME) {
        return 3;
    }
    return 4;
}

static int fdn_windows_name(fdn_string *name, const wchar_t *value) {
    char *bytes;
    const size_t wide_length = wcslen(value);
    int length;
    if (wide_length > (size_t)INT_MAX) {
        return 0;
    }
    length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, (int)wide_length,
                                 NULL, 0, NULL, NULL);
    if (length == 0 && wide_length != 0) {
        return 0;
    }
    if (length == 0) {
        *name = fdn_string_static("", 0);
        return 1;
    }
    bytes = fdn_alloc((size_t)length);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, (int)wide_length,
                            bytes, length, NULL, NULL) != length) {
        fdn_dealloc(bytes);
        return 0;
    }
    name->data = bytes;
    name->length = (size_t)length;
    name->owned = 1;
    return 1;
}
#else
typedef struct fdn_directory_handle {
    DIR *directory;
} fdn_directory_handle;
#endif

int32_t foundation_runtime_fs_open_directory(const fdn_string *path, uint64_t *handle) {
    fdn_directory_handle *state;
    if (handle == NULL) {
        fdn_panic_cstr("directory handle output is null");
    }
    *handle = 0;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_windows_path(path);
        wchar_t *pattern;
        size_t length;
        DWORD attributes;
        if (native_path == NULL) {
            return 3;
        }
        attributes = GetFileAttributesW(native_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            fdn_dealloc(native_path);
            return status;
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            fdn_dealloc(native_path);
            return 3;
        }
        length = wcslen(native_path);
        pattern = fdn_alloc((length + 3) * sizeof(*pattern));
        (void)memcpy(pattern, native_path, length * sizeof(*pattern));
        fdn_dealloc(native_path);
        if (length != 0 && pattern[length - 1] != L'\\' && pattern[length - 1] != L'/') {
            pattern[length++] = L'\\';
        }
        pattern[length++] = L'*';
        pattern[length] = L'\0';
        state = fdn_alloc(sizeof(*state));
        state->search = FindFirstFileW(pattern, &state->entry);
        state->first = 1;
        fdn_dealloc(pattern);
        if (state->search == INVALID_HANDLE_VALUE) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            fdn_dealloc(state);
            return status;
        }
    }
#else
    {
        char *native_path = fdn_native_path(path);
        if (native_path == NULL) {
            return 3;
        }
        state = fdn_alloc(sizeof(*state));
        state->directory = opendir(native_path);
        fdn_dealloc(native_path);
        if (state->directory == NULL) {
            const int32_t status = fdn_fs_status(errno);
            fdn_dealloc(state);
            return status;
        }
    }
#endif
    *handle = (uint64_t)(uintptr_t)state;
    fdn_handle_count_add(&fdn_live_directory_count);
    return 0;
}

int32_t foundation_runtime_fs_next_directory(uint64_t handle, fdn_string *name) {
    fdn_directory_handle *state = (fdn_directory_handle *)(uintptr_t)handle;
    if (state == NULL || name == NULL) {
        return 3;
    }
    fdn_string_drop(name);
    *name = fdn_string_static("", 0);
#if defined(_WIN32)
    while (1) {
        const wchar_t *value;
        if (state->first != 0) {
            state->first = 0;
        } else if (FindNextFileW(state->search, &state->entry) == 0) {
            return GetLastError() == ERROR_NO_MORE_FILES ? 0 : 3;
        }
        value = state->entry.cFileName;
        if (wcscmp(value, L".") == 0 || wcscmp(value, L"..") == 0) {
            continue;
        }
        return fdn_windows_name(name, value) ? 1 : 2;
    }
#else
    while (1) {
        struct dirent *entry;
        size_t length;
        errno = 0;
        entry = readdir(state->directory);
        if (entry == NULL) {
            return errno == 0 ? 0 : 3;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        length = strlen(entry->d_name);
        if (!fdn_utf8_valid(entry->d_name, length)) {
            return 2;
        }
        *name = fdn_string_copy(entry->d_name, length);
        return 1;
    }
#endif
}

int32_t foundation_runtime_fs_close_directory(uint64_t handle) {
    fdn_directory_handle *state = (fdn_directory_handle *)(uintptr_t)handle;
    int status;
    if (state == NULL) {
        return 0;
    }
#if defined(_WIN32)
    status = FindClose(state->search) != 0 ? 0 : 4;
#else
    status = closedir(state->directory) == 0 ? 0 : 4;
#endif
    fdn_dealloc(state);
    fdn_handle_count_remove(&fdn_live_directory_count);
    return status;
}

int32_t foundation_runtime_fs_is_directory(const fdn_string *path, bool *result) {
    if (result == NULL) {
        fdn_panic_cstr("directory result is null");
    }
    *result = false;
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_windows_path(path);
        DWORD attributes;
        if (native_path == NULL) {
            return 3;
        }
        attributes = GetFileAttributesW(native_path);
        fdn_dealloc(native_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return fdn_windows_fs_status(GetLastError());
        }
        *result = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
#else
    {
        struct stat info;
        char *native_path = fdn_native_path(path);
        int status;
        if (native_path == NULL) {
            return 3;
        }
        status = stat(native_path, &info);
        fdn_dealloc(native_path);
        if (status != 0) {
            return fdn_fs_status(errno);
        }
        *result = S_ISDIR(info.st_mode);
    }
#endif
    return 0;
}

int32_t foundation_runtime_fs_modified(const fdn_string *path, uint64_t *unix_seconds) {
    if (unix_seconds == NULL) {
        fdn_panic_cstr("file modification output is null");
    }
    *unix_seconds = 0;
#if defined(_WIN32)
    {
        struct _stat64 info;
        wchar_t *native_path = fdn_windows_path(path);
        int status;
        if (native_path == NULL) {
            return 3;
        }
        status = _wstat64(native_path, &info);
        fdn_dealloc(native_path);
        if (status != 0) {
            return fdn_fs_status(errno);
        }
        if (info.st_mtime < 0) {
            return 4;
        }
        *unix_seconds = (uint64_t)info.st_mtime;
    }
#else
    {
        struct stat info;
        char *native_path = fdn_native_path(path);
        int status;
        if (native_path == NULL) {
            return 3;
        }
        status = stat(native_path, &info);
        fdn_dealloc(native_path);
        if (status != 0) {
            return fdn_fs_status(errno);
        }
        if (info.st_mtime < 0) {
            return 4;
        }
        *unix_seconds = (uint64_t)info.st_mtime;
    }
#endif
    return 0;
}

int32_t foundation_runtime_fs_read_text_limited(const fdn_string *path, uint64_t max_length,
                                                fdn_string *result) {
    char chunk[4096];
    char *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const size_t limit = max_length > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)max_length;
    int32_t status;
    FILE *file;
    if (result == NULL) {
        fdn_panic_cstr("file text output is null");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
    file = fdn_fs_open_file(path, &status);
    if (file == NULL) {
        return status;
    }
    for (;;) {
        const size_t count = fread(chunk, 1, sizeof(chunk), file);
        if (count != 0) {
            size_t required;
            if (count > limit - length) {
                fdn_dealloc(data);
                (void)fclose(file);
                return 5;
            }
            required = length + count;
            if (required > capacity) {
                size_t next_capacity = capacity == 0 ? sizeof(chunk) : capacity;
                char *grown;
                if (next_capacity > limit) {
                    next_capacity = limit;
                }
                while (next_capacity < required) {
                    if (next_capacity > limit / 2) {
                        next_capacity = limit;
                    } else {
                        next_capacity *= 2;
                    }
                }
                grown = fdn_alloc(next_capacity);
                if (length != 0) {
                    (void)memcpy(grown, data, length);
                }
                fdn_dealloc(data);
                data = grown;
                capacity = next_capacity;
            }
            (void)memcpy(data + length, chunk, count);
            length = required;
        }
        if (count != sizeof(chunk)) {
            if (ferror(file) != 0) {
                fdn_dealloc(data);
                (void)fclose(file);
                return 4;
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        fdn_dealloc(data);
        return 4;
    }
    if (!fdn_utf8_valid(data, length)) {
        fdn_dealloc(data);
        return 6;
    }
    if (length == 0) {
        fdn_dealloc(data);
        return 0;
    }
    result->data = data;
    result->length = length;
    result->owned = 1;
    return 0;
}

uint64_t foundation_runtime_fs_live_directories(void) {
    return fdn_handle_count_read(&fdn_live_directory_count);
}
