#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "foundation/runtime.h"
#include "bytes_internal.h"

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

#if defined(__APPLE__)
extern void arc4random_buf(void *buffer, size_t length);
#endif

_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "runtime handles require a 64-bit carrier");

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <share.h>
#include <wchar.h>
#include <windows.h>
#include <bcrypt.h>
static SRWLOCK fdn_stdout_lock = SRWLOCK_INIT;
static SRWLOCK fdn_uuid_lock = SRWLOCK_INIT;
#else
#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <unistd.h>
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

void fdn_abi_string_concat(fdn_string *result, const fdn_string *left,
                           const fdn_string *right) {
    *result = fdn_string_concat(*left, *right);
}

int fdn_abi_string_equal(const fdn_string *left, const fdn_string *right) {
    return fdn_string_equal(*left, *right);
}

void fdn_abi_println(const fdn_string *value) { fdn_println(*value); }

void fdn_abi_panic(const fdn_string *message) { fdn_panic(*message); }

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

int32_t fdn_bytes_adopt(uint8_t *data, size_t length, size_t capacity,
                        uint64_t *result) {
    fdn_bytes *value;
    if (result == NULL || length > capacity || (data == NULL && capacity != 0)) {
        return 1;
    }
    value = fdn_alloc(sizeof(*value));
    value->data = data;
    value->length = length;
    value->capacity = capacity;
    *result = (uint64_t)(uintptr_t)value;
    return 0;
}

int32_t fdn_bytes_view(uint64_t handle, const uint8_t **data, size_t *length) {
    const fdn_bytes *value = (const fdn_bytes *)(uintptr_t)handle;
    if (value == NULL || data == NULL || length == NULL) {
        return 1;
    }
    *data = value->data;
    *length = value->length;
    return 0;
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

#define FDN_DEFINE_SIGNED_ARITHMETIC(TYPE, UNSIGNED, NAME, MINIMUM, MAXIMUM) \
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
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        UNSIGNED factor; \
        if (right < 0 || (uintmax_t)right >= (uintmax_t)width) { \
            fdn_arithmetic_panic("shift count out of range"); \
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
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        factor = (UNSIGNED)1 << (unsigned int)right; \
        if ((left > 0 && left > (TYPE)((MAXIMUM) / (TYPE)factor)) || \
            (left < 0 && left < (TYPE)((MINIMUM) / (TYPE)factor))) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left * (TYPE)factor); \
    } \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if (right < 0 || (uintmax_t)right >= (uintmax_t)width) { \
            fdn_arithmetic_panic("shift count out of range"); \
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
    } \
    TYPE fdn_##NAME##_shift_left(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if ((uintmax_t)right >= (uintmax_t)width) { \
            fdn_arithmetic_panic("shift count out of range"); \
        } \
        if (right != 0 && left > (TYPE)((MAXIMUM) >> (unsigned int)right)) { \
            fdn_arithmetic_panic(#NAME " overflow"); \
        } \
        return (TYPE)(left << (unsigned int)right); \
    } \
    TYPE fdn_##NAME##_shift_right(TYPE left, TYPE right) { \
        const unsigned int width = (unsigned int)(sizeof(TYPE) * CHAR_BIT); \
        if ((uintmax_t)right >= (uintmax_t)width) { \
            fdn_arithmetic_panic("shift count out of range"); \
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

typedef struct fdn_guard_roles {
    fdn_string *values;
    size_t length;
    size_t capacity;
    size_t limit;
} fdn_guard_roles;

typedef struct fdn_guard_relationship {
    fdn_string identity;
    fdn_string role;
} fdn_guard_relationship;

typedef struct fdn_guard_relationships {
    fdn_guard_relationship *values;
    size_t length;
    size_t capacity;
    size_t limit;
} fdn_guard_relationships;

static bool fdn_guard_text_equal(const fdn_string *left, const fdn_string *right) {
    return left != NULL && right != NULL && left->length == right->length &&
           (left->length == 0 || memcmp(left->data, right->data, left->length) == 0);
}

static bool fdn_guard_text_valid(const fdn_string *value) {
    return value != NULL && value->length != 0 && value->data != NULL &&
           fdn_utf8_valid(value->data, value->length);
}

static bool fdn_guard_reserve(void **values, size_t element_size, size_t used,
                              size_t *capacity, size_t required, size_t limit) {
    size_t next;
    void *replacement;

    if (required <= *capacity) {
        return true;
    }
    if (required > limit || element_size == 0) {
        return false;
    }
    next = *capacity == 0 ? 4 : *capacity;
    while (next < required) {
        if (next > limit / 2) {
            next = limit;
            break;
        }
        next *= 2;
    }
    if (next > limit) {
        next = limit;
    }
    if (next < required || next > SIZE_MAX / element_size) {
        return false;
    }
    replacement = fdn_alloc(next * element_size);
    if (*values != NULL) {
        (void)memcpy(replacement, *values, used * element_size);
        fdn_dealloc(*values);
    }
    *values = replacement;
    *capacity = next;
    return true;
}

static fdn_guard_roles *fdn_guard_roles_create(size_t limit) {
    fdn_guard_roles *roles = fdn_alloc(sizeof(*roles));
    roles->values = NULL;
    roles->length = 0;
    roles->capacity = 0;
    roles->limit = limit;
    return roles;
}

static int32_t fdn_guard_roles_add(fdn_guard_roles *roles, const fdn_string *role) {
    size_t index;

    if (roles == NULL) {
        return 4;
    }
    if (!fdn_guard_text_valid(role)) {
        return 2;
    }
    for (index = 0; index < roles->length; ++index) {
        if (fdn_guard_text_equal(&roles->values[index], role)) {
            return 1;
        }
    }
    if (roles->length == roles->limit ||
        !fdn_guard_reserve((void **)&roles->values, sizeof(*roles->values), roles->length,
                           &roles->capacity, roles->length + 1, roles->limit)) {
        return 3;
    }
    roles->values[roles->length++] = foundation_runtime_string_copy(role);
    return 0;
}

int32_t foundation_runtime_guard_roles_open(uint64_t limit, uint64_t *result) {
    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (limit == 0 || limit > UINT64_C(65536) || limit > (uint64_t)SIZE_MAX) {
        return 1;
    }
    *result = (uint64_t)(uintptr_t)fdn_guard_roles_create((size_t)limit);
    return 0;
}

int32_t foundation_runtime_guard_roles_add(uint64_t handle, const fdn_string *role) {
    return fdn_guard_roles_add((fdn_guard_roles *)(uintptr_t)handle, role);
}

bool foundation_runtime_guard_roles_has(uint64_t handle, const fdn_string *role) {
    const fdn_guard_roles *roles = (const fdn_guard_roles *)(uintptr_t)handle;
    size_t index;

    if (roles == NULL || !fdn_guard_text_valid(role)) {
        return false;
    }
    for (index = 0; index < roles->length; ++index) {
        if (fdn_guard_text_equal(&roles->values[index], role)) {
            return true;
        }
    }
    return false;
}

int32_t foundation_runtime_guard_roles_count(uint64_t handle, uint64_t *result) {
    const fdn_guard_roles *roles = (const fdn_guard_roles *)(uintptr_t)handle;
    if (roles == NULL || result == NULL) {
        return 1;
    }
    *result = (uint64_t)roles->length;
    return 0;
}

int32_t foundation_runtime_guard_roles_at(uint64_t handle, uint64_t index,
                                          fdn_string *result) {
    const fdn_guard_roles *roles = (const fdn_guard_roles *)(uintptr_t)handle;
    if (roles == NULL || result == NULL) {
        return 1;
    }
    if (index >= roles->length) {
        return 2;
    }
    fdn_string_drop(result);
    *result = foundation_runtime_string_copy(&roles->values[(size_t)index]);
    return 0;
}

void foundation_runtime_guard_roles_close(uint64_t *handle) {
    fdn_guard_roles *roles;
    size_t index;

    if (handle == NULL || *handle == 0) {
        return;
    }
    roles = (fdn_guard_roles *)(uintptr_t)*handle;
    *handle = 0;
    for (index = 0; index < roles->length; ++index) {
        fdn_string_drop(&roles->values[index]);
    }
    fdn_dealloc(roles->values);
    fdn_dealloc(roles);
}

int32_t foundation_runtime_guard_relationships_open(uint64_t limit, uint64_t *result) {
    fdn_guard_relationships *relationships;

    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (limit == 0 || limit > UINT64_C(65536) || limit > (uint64_t)SIZE_MAX) {
        return 1;
    }
    relationships = fdn_alloc(sizeof(*relationships));
    relationships->values = NULL;
    relationships->length = 0;
    relationships->capacity = 0;
    relationships->limit = (size_t)limit;
    *result = (uint64_t)(uintptr_t)relationships;
    return 0;
}

int32_t foundation_runtime_guard_relationships_add(uint64_t handle,
                                                   const fdn_string *identity,
                                                   const fdn_string *role) {
    fdn_guard_relationships *relationships =
        (fdn_guard_relationships *)(uintptr_t)handle;
    size_t index;

    if (relationships == NULL) {
        return 4;
    }
    if (!fdn_guard_text_valid(identity) || !fdn_guard_text_valid(role)) {
        return 2;
    }
    for (index = 0; index < relationships->length; ++index) {
        if (fdn_guard_text_equal(&relationships->values[index].identity, identity) &&
            fdn_guard_text_equal(&relationships->values[index].role, role)) {
            return 1;
        }
    }
    if (relationships->length == relationships->limit ||
        !fdn_guard_reserve((void **)&relationships->values,
                           sizeof(*relationships->values), relationships->length,
                           &relationships->capacity, relationships->length + 1,
                           relationships->limit)) {
        return 3;
    }
    relationships->values[relationships->length].identity =
        foundation_runtime_string_copy(identity);
    relationships->values[relationships->length].role =
        foundation_runtime_string_copy(role);
    relationships->length += 1;
    return 0;
}

int32_t foundation_runtime_guard_relationships_roles(uint64_t handle,
                                                     const fdn_string *identity,
                                                     uint64_t *result) {
    const fdn_guard_relationships *relationships =
        (const fdn_guard_relationships *)(uintptr_t)handle;
    fdn_guard_roles *roles;
    size_t index;

    if (result == NULL) {
        return 1;
    }
    *result = 0;
    if (relationships == NULL || !fdn_guard_text_valid(identity)) {
        return 1;
    }
    roles = fdn_guard_roles_create((size_t)UINT64_C(65536));
    for (index = 0; index < relationships->length; ++index) {
        if (fdn_guard_text_equal(&relationships->values[index].identity, identity)) {
            const int32_t status =
                fdn_guard_roles_add(roles, &relationships->values[index].role);
            if (status != 0 && status != 1) {
                uint64_t role_handle = (uint64_t)(uintptr_t)roles;
                foundation_runtime_guard_roles_close(&role_handle);
                return 1;
            }
        }
    }
    *result = (uint64_t)(uintptr_t)roles;
    return 0;
}

void foundation_runtime_guard_relationships_close(uint64_t *handle) {
    fdn_guard_relationships *relationships;
    size_t index;

    if (handle == NULL || *handle == 0) {
        return;
    }
    relationships = (fdn_guard_relationships *)(uintptr_t)*handle;
    *handle = 0;
    for (index = 0; index < relationships->length; ++index) {
        fdn_string_drop(&relationships->values[index].identity);
        fdn_string_drop(&relationships->values[index].role);
    }
    fdn_dealloc(relationships->values);
    fdn_dealloc(relationships);
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
    if (feof(file) != 0) {
        clearerr(file);
    }
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

typedef struct fdn_fs_tree_entry {
    char *path;
    size_t path_length;
    uint64_t size;
    uint32_t kind;
    bool executable;
} fdn_fs_tree_entry;

typedef struct fdn_fs_tree {
    fdn_fs_tree_entry *entries;
    size_t count;
    size_t capacity;
    size_t cursor;
    size_t max_entries;
    size_t max_path_length;
#if defined(_WIN32)
    wchar_t *root;
    HANDLE root_handle;
#else
    int root;
#endif
} fdn_fs_tree;

typedef struct fdn_fs_root {
#if defined(_WIN32)
    wchar_t *path;
    HANDLE directory;
#else
    int descriptor;
#endif
} fdn_fs_root;

static int fdn_fs_relative_path_valid(const fdn_string *path) {
    size_t offset;
    size_t segment_start = 0;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        !fdn_utf8_valid(path->data, path->length) || path->data[0] == '/' ||
        path->data[path->length - 1] == '/') {
        return 0;
    }
    for (offset = 0; offset <= path->length; ++offset) {
        const int at_end = offset == path->length;
        if (!at_end && path->data[offset] != '/') {
            if (path->data[offset] == '\\' || path->data[offset] == '\0') {
                return 0;
            }
            continue;
        }
        if (offset == segment_start ||
            (offset - segment_start == 1 && path->data[segment_start] == '.') ||
            (offset - segment_start == 2 && path->data[segment_start] == '.' &&
             path->data[segment_start + 1] == '.')) {
            return 0;
        }
        segment_start = offset + 1;
    }
    return 1;
}

static int fdn_fs_tree_entry_compare(const void *left, const void *right) {
    const fdn_fs_tree_entry *left_entry = left;
    const fdn_fs_tree_entry *right_entry = right;
    const size_t shared = left_entry->path_length < right_entry->path_length
                              ? left_entry->path_length
                              : right_entry->path_length;
    const int compared = memcmp(left_entry->path, right_entry->path, shared);
    if (compared != 0) {
        return compared;
    }
    if (left_entry->path_length < right_entry->path_length) {
        return -1;
    }
    return left_entry->path_length > right_entry->path_length ? 1 : 0;
}

static int32_t fdn_fs_tree_append(fdn_fs_tree *tree, const char *path,
                                  size_t path_length, uint32_t kind,
                                  bool executable, uint64_t size) {
    fdn_fs_tree_entry *grown;
    size_t next_capacity;
    if (path_length == 0 || path_length > tree->max_path_length ||
        tree->count >= tree->max_entries) {
        return 5;
    }
    if (tree->count == tree->capacity) {
        next_capacity = tree->capacity == 0 ? 32 : tree->capacity * 2;
        if (next_capacity < tree->capacity || next_capacity > tree->max_entries) {
            next_capacity = tree->max_entries;
        }
        if (next_capacity <= tree->capacity ||
            next_capacity > SIZE_MAX / sizeof(*grown)) {
            return 5;
        }
        grown = fdn_alloc(next_capacity * sizeof(*grown));
        if (tree->count != 0) {
            (void)memcpy(grown, tree->entries,
                         tree->count * sizeof(*grown));
        }
        fdn_dealloc(tree->entries);
        tree->entries = grown;
        tree->capacity = next_capacity;
    }
    tree->entries[tree->count].path = fdn_alloc(path_length + 1);
    (void)memcpy(tree->entries[tree->count].path, path, path_length);
    tree->entries[tree->count].path[path_length] = '\0';
    tree->entries[tree->count].path_length = path_length;
    tree->entries[tree->count].size = size;
    tree->entries[tree->count].kind = kind;
    tree->entries[tree->count].executable = executable;
    ++tree->count;
    return 0;
}

static void fdn_fs_tree_release(fdn_fs_tree *tree) {
    size_t index;
    if (tree == NULL) {
        return;
    }
    for (index = 0; index < tree->count; ++index) {
        fdn_dealloc(tree->entries[index].path);
    }
    fdn_dealloc(tree->entries);
#if defined(_WIN32)
    if (tree->root_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(tree->root_handle);
    }
    fdn_dealloc(tree->root);
#else
    if (tree->root >= 0) {
        (void)close(tree->root);
    }
#endif
    fdn_dealloc(tree);
}

#if defined(_WIN32)
static wchar_t *fdn_windows_join_relative(const wchar_t *root,
                                          const fdn_string *relative_path) {
    wchar_t *relative;
    wchar_t *result;
    size_t root_length;
    size_t relative_length;
    size_t index;
    if (!fdn_fs_relative_path_valid(relative_path)) {
        return NULL;
    }
    relative = fdn_windows_path(relative_path);
    if (relative == NULL) {
        return NULL;
    }
    root_length = wcslen(root);
    relative_length = wcslen(relative);
    if (root_length > SIZE_MAX - relative_length - 2 ||
        root_length + relative_length + 2 > SIZE_MAX / sizeof(*result)) {
        fdn_dealloc(relative);
        return NULL;
    }
    result = fdn_alloc((root_length + relative_length + 2) * sizeof(*result));
    (void)memcpy(result, root, root_length * sizeof(*result));
    if (root_length != 0 && result[root_length - 1] != L'\\' &&
        result[root_length - 1] != L'/') {
        result[root_length++] = L'\\';
    }
    for (index = 0; index < relative_length; ++index) {
        result[root_length + index] = relative[index] == L'/'
                                          ? L'\\'
                                          : relative[index];
    }
    result[root_length + relative_length] = L'\0';
    fdn_dealloc(relative);
    return result;
}

static HANDLE fdn_windows_open_directory_guard(const wchar_t *path) {
    HANDLE directory = CreateFileW(
        path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    if (directory == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    if (GetFileInformationByHandle(directory, &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        (void)CloseHandle(directory);
        SetLastError(ERROR_INVALID_NAME);
        return INVALID_HANDLE_VALUE;
    }
    return directory;
}

static void fdn_windows_close_directory_guards(HANDLE *guards,
                                               size_t count) {
    while (count > 0) {
        --count;
        (void)CloseHandle(guards[count]);
    }
}

static int32_t fdn_windows_lock_relative_directories(
    const wchar_t *root, const fdn_string *relative_path, int include_final,
    HANDLE guards[129], size_t *guard_count) {
    wchar_t *path = fdn_windows_join_relative(root, relative_path);
    const size_t root_length = wcslen(root);
    size_t offset;
    if (path == NULL || guard_count == NULL) {
        fdn_dealloc(path);
        return 3;
    }
    *guard_count = 0;
    for (offset = root_length + 1; ; ++offset) {
        const int at_end = path[offset] == L'\0';
        wchar_t saved;
        HANDLE guard;
        if (!at_end && path[offset] != L'\\' && path[offset] != L'/') {
            continue;
        }
        if (at_end && include_final == 0) {
            break;
        }
        if (*guard_count >= 129) {
            fdn_windows_close_directory_guards(guards, *guard_count);
            *guard_count = 0;
            fdn_dealloc(path);
            return 5;
        }
        saved = path[offset];
        path[offset] = L'\0';
        guard = fdn_windows_open_directory_guard(path);
        path[offset] = saved;
        if (guard == INVALID_HANDLE_VALUE) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            fdn_windows_close_directory_guards(guards, *guard_count);
            *guard_count = 0;
            fdn_dealloc(path);
            return status;
        }
        guards[(*guard_count)++] = guard;
        if (at_end) {
            break;
        }
    }
    fdn_dealloc(path);
    return 0;
}

static int32_t fdn_windows_tree_collect(fdn_fs_tree *tree,
                                        const wchar_t *directory,
                                        const char *prefix,
                                        size_t prefix_length,
                                        size_t depth) {
    WIN32_FIND_DATAW native_entry;
    HANDLE search;
    wchar_t *pattern;
    size_t directory_length;
    int32_t status = 0;
    if (depth > 128) {
        return 5;
    }
    directory_length = wcslen(directory);
    if (directory_length > SIZE_MAX - 3 ||
        directory_length + 3 > SIZE_MAX / sizeof(*pattern)) {
        return 5;
    }
    pattern = fdn_alloc((directory_length + 3) * sizeof(*pattern));
    (void)memcpy(pattern, directory, directory_length * sizeof(*pattern));
    if (directory_length != 0 && pattern[directory_length - 1] != L'\\' &&
        pattern[directory_length - 1] != L'/') {
        pattern[directory_length++] = L'\\';
    }
    pattern[directory_length++] = L'*';
    pattern[directory_length] = L'\0';
    search = FindFirstFileW(pattern, &native_entry);
    fdn_dealloc(pattern);
    if (search == INVALID_HANDLE_VALUE) {
        return fdn_windows_fs_status(GetLastError());
    }
    for (;;) {
        const wchar_t *native_name = native_entry.cFileName;
        if (wcscmp(native_name, L".") != 0 && wcscmp(native_name, L"..") != 0) {
            fdn_string converted = fdn_string_static("", 0);
            const size_t separator = prefix_length == 0 ? 0 : 1;
            size_t path_length;
            char *path;
            uint32_t kind;
            bool executable = false;
            uint64_t size = 0;
            if (!fdn_windows_name(&converted, native_name) ||
                prefix_length > SIZE_MAX - separator ||
                prefix_length + separator > SIZE_MAX - converted.length) {
                fdn_string_drop(&converted);
                status = 6;
                break;
            }
            path_length = prefix_length + separator + converted.length;
            if (path_length > tree->max_path_length) {
                fdn_string_drop(&converted);
                status = 5;
                break;
            }
            path = fdn_alloc(path_length + 1);
            if (prefix_length != 0) {
                (void)memcpy(path, prefix, prefix_length);
                path[prefix_length] = '/';
            }
            if (converted.length != 0) {
                (void)memcpy(path + prefix_length + separator,
                             converted.data, converted.length);
            }
            path[path_length] = '\0';
            if ((native_entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                kind = 3;
            } else if ((native_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                kind = 2;
            } else if ((native_entry.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
                kind = 4;
            } else {
                kind = 1;
                size = ((uint64_t)native_entry.nFileSizeHigh << 32U) |
                       (uint64_t)native_entry.nFileSizeLow;
            }
            status = fdn_fs_tree_append(tree, path, path_length, kind,
                                        executable, size);
            if (status == 0 && kind == 2) {
                wchar_t *child;
                HANDLE guard;
                const size_t name_length = wcslen(native_name);
                size_t child_length = wcslen(directory);
                if (child_length > SIZE_MAX - name_length - 2 ||
                    child_length + name_length + 2 > SIZE_MAX / sizeof(*child)) {
                    status = 5;
                } else {
                    child = fdn_alloc((child_length + name_length + 2) *
                                      sizeof(*child));
                    (void)memcpy(child, directory,
                                 child_length * sizeof(*child));
                    if (child_length != 0 && child[child_length - 1] != L'\\' &&
                        child[child_length - 1] != L'/') {
                        child[child_length++] = L'\\';
                    }
                    (void)memcpy(child + child_length, native_name,
                                 (name_length + 1) * sizeof(*child));
                    guard = fdn_windows_open_directory_guard(child);
                    if (guard == INVALID_HANDLE_VALUE) {
                        status = fdn_windows_fs_status(GetLastError());
                    } else {
                        status = fdn_windows_tree_collect(tree, child, path,
                                                          path_length,
                                                          depth + 1);
                        (void)CloseHandle(guard);
                    }
                    fdn_dealloc(child);
                }
            }
            fdn_dealloc(path);
            fdn_string_drop(&converted);
            if (status != 0) {
                break;
            }
        }
        if (FindNextFileW(search, &native_entry) == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                status = fdn_windows_fs_status(error);
            }
            break;
        }
    }
    if (FindClose(search) == 0 && status == 0) {
        status = fdn_windows_fs_status(GetLastError());
    }
    return status;
}

static int32_t fdn_windows_read_tree_file(const wchar_t *root,
                                          const fdn_string *relative_path,
                                          uint64_t max_length,
                                          uint64_t *bytes_handle) {
    wchar_t *path = fdn_windows_join_relative(root, relative_path);
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    uint8_t *data = NULL;
    size_t length;
    size_t offset = 0;
    uint64_t native_length;
    int32_t status = 0;
    HANDLE guards[129];
    size_t guard_count = 0;
    if (bytes_handle == NULL) {
        fdn_dealloc(path);
        return 4;
    }
    *bytes_handle = 0;
    if (path == NULL) {
        return 3;
    }
    status = fdn_windows_lock_relative_directories(
        root, relative_path, 0, guards, &guard_count);
    if (status != 0) {
        fdn_dealloc(path);
        return status;
    }
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT |
                           FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    fdn_dealloc(path);
    if (file == INVALID_HANDLE_VALUE) {
        status = fdn_windows_fs_status(GetLastError());
        fdn_windows_close_directory_guards(guards, guard_count);
        return status;
    }
    if (GetFileInformationByHandle(file, &info) == 0 ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
        (void)CloseHandle(file);
        fdn_windows_close_directory_guards(guards, guard_count);
        return 3;
    }
    native_length = ((uint64_t)info.nFileSizeHigh << 32U) |
                    (uint64_t)info.nFileSizeLow;
    if (native_length > max_length || native_length > SIZE_MAX) {
        (void)CloseHandle(file);
        fdn_windows_close_directory_guards(guards, guard_count);
        return 5;
    }
    length = (size_t)native_length;
    if (length != 0) {
        data = fdn_alloc(length);
    }
    while (offset < length) {
        const size_t remaining = length - offset;
        const DWORD requested = remaining > UINT32_MAX
                                    ? UINT32_MAX
                                    : (DWORD)remaining;
        DWORD count = 0;
        if (ReadFile(file, data + offset, requested, &count, NULL) == 0 ||
            count == 0) {
            status = 4;
            break;
        }
        offset += (size_t)count;
    }
    if (status == 0) {
        uint8_t probe;
        DWORD count = 0;
        if (ReadFile(file, &probe, 1, &count, NULL) == 0) {
            status = fdn_windows_fs_status(GetLastError());
        } else if (count != 0) {
            status = 5;
        }
    }
    if (CloseHandle(file) == 0 && status == 0) {
        status = 4;
    }
    fdn_windows_close_directory_guards(guards, guard_count);
    if (status == 0 && fdn_bytes_adopt(data, offset, length, bytes_handle) == 0) {
        return 0;
    }
    if (data != NULL) {
        (void)memset(data, 0, length);
        fdn_dealloc(data);
    }
    return status == 0 ? 4 : status;
}

static int32_t fdn_windows_create_root_directory(fdn_fs_root *root,
                                                 const fdn_string *relative_path) {
    wchar_t *path = fdn_windows_join_relative(root->path, relative_path);
    const size_t root_length = wcslen(root->path);
    size_t offset;
    HANDLE guards[129];
    size_t guard_count = 0;
    if (path == NULL) {
        return 3;
    }
    for (offset = root_length + 1; ; ++offset) {
        wchar_t saved;
        DWORD attributes;
        if (path[offset] != L'\0' && path[offset] != L'\\' && path[offset] != L'/') {
            continue;
        }
        saved = path[offset];
        path[offset] = L'\0';
        if (CreateDirectoryW(path, NULL) == 0 &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            path[offset] = saved;
            fdn_windows_close_directory_guards(guards, guard_count);
            fdn_dealloc(path);
            return status;
        }
        attributes = GetFileAttributesW(path);
        path[offset] = saved;
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            fdn_windows_close_directory_guards(guards, guard_count);
            fdn_dealloc(path);
            return 3;
        }
        path[offset] = L'\0';
        if (guard_count >= 129) {
            path[offset] = saved;
            fdn_windows_close_directory_guards(guards, guard_count);
            fdn_dealloc(path);
            return 5;
        }
        guards[guard_count] = fdn_windows_open_directory_guard(path);
        path[offset] = saved;
        if (guards[guard_count] == INVALID_HANDLE_VALUE) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            fdn_windows_close_directory_guards(guards, guard_count);
            fdn_dealloc(path);
            return status;
        }
        ++guard_count;
        if (saved == L'\0') {
            break;
        }
    }
    fdn_windows_close_directory_guards(guards, guard_count);
    fdn_dealloc(path);
    return 0;
}

static int32_t fdn_windows_write_root_file(fdn_fs_root *root,
                                           const fdn_string *relative_path,
                                           uint64_t bytes_handle,
                                           uint32_t permissions) {
    static volatile LONG64 sequence;
    const uint8_t *data;
    size_t length;
    fdn_string parent_relative = fdn_string_static("", 0);
    size_t separator = relative_path == NULL ? 0 : relative_path->length;
    wchar_t *target;
    wchar_t *last_separator;
    wchar_t temporary[32768];
    bool temporary_created = false;
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t offset = 0;
    int32_t status = 4;
    HANDLE guards[129];
    size_t guard_count = 0;
    if (!fdn_fs_relative_path_valid(relative_path) || permissions > 511 ||
        fdn_bytes_view(bytes_handle, &data, &length) != 0) {
        return 3;
    }
    while (separator > 0 && relative_path->data[separator - 1] != '/') {
        --separator;
    }
    if (separator > 0) {
        parent_relative = fdn_string_static(relative_path->data, separator - 1);
        status = fdn_windows_create_root_directory(root, &parent_relative);
        if (status != 0) {
            return status;
        }
    }
    target = fdn_windows_join_relative(root->path, relative_path);
    if (target == NULL || wcslen(target) >= 32700) {
        fdn_dealloc(target);
        return 3;
    }
    status = fdn_windows_lock_relative_directories(
        root->path, relative_path, 0, guards, &guard_count);
    if (status != 0) {
        fdn_dealloc(target);
        return status;
    }
    {
        const DWORD attributes = GetFileAttributesW(target);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                           FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            fdn_dealloc(target);
            fdn_windows_close_directory_guards(guards, guard_count);
            return 3;
        }
    }
    last_separator = wcsrchr(target, L'\\');
    if (last_separator == NULL) {
        fdn_dealloc(target);
        fdn_windows_close_directory_guards(guards, guard_count);
        return 3;
    }
    for (;;) {
        const LONG64 current = InterlockedIncrement64(&sequence);
        const size_t parent_length = (size_t)(last_separator - target);
        int written;
        (void)memcpy(temporary, target, parent_length * sizeof(*temporary));
        written = _snwprintf_s(temporary + parent_length,
                               sizeof(temporary) / sizeof(*temporary) - parent_length,
                               _TRUNCATE, L"\\.foundation-%lu-%lld.tmp",
                               (unsigned long)GetCurrentProcessId(),
                               (long long)current);
        if (written < 0) {
            status = 4;
            goto cleanup;
        }
        file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY |
                               FILE_FLAG_OPEN_REPARSE_POINT,
                           NULL);
        if (file != INVALID_HANDLE_VALUE) {
            temporary_created = true;
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            status = fdn_windows_fs_status(GetLastError());
            goto cleanup;
        }
    }
    while (offset < length) {
        const size_t remaining = length - offset;
        const DWORD requested = remaining > UINT32_MAX
                                    ? UINT32_MAX
                                    : (DWORD)remaining;
        DWORD written = 0;
        if (WriteFile(file, data + offset, requested, &written, NULL) == 0 ||
            written == 0) {
            status = 4;
            goto cleanup;
        }
        offset += (size_t)written;
    }
    if (FlushFileBuffers(file) == 0) {
        status = 4;
        goto cleanup;
    }
    if (CloseHandle(file) == 0) {
        file = INVALID_HANDLE_VALUE;
        status = 4;
        goto cleanup;
    }
    file = INVALID_HANDLE_VALUE;
    if (MoveFileExW(temporary, target,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        status = fdn_windows_fs_status(GetLastError());
        goto cleanup;
    }
    temporary_created = false;
    status = 0;

cleanup:
    if (file != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(file);
    }
    if (temporary_created) {
        (void)DeleteFileW(temporary);
    }
    fdn_dealloc(target);
    fdn_windows_close_directory_guards(guards, guard_count);
    return status;
}
#endif

#if !defined(_WIN32)
static int32_t fdn_posix_tree_collect(fdn_fs_tree *tree, int directory,
                                      const char *prefix, size_t prefix_length,
                                      size_t depth) {
    DIR *stream;
    struct dirent *entry;
    int duplicated;
    int32_t status = 0;
    if (depth > 128) {
        return 5;
    }
    duplicated = dup(directory);
    if (duplicated < 0) {
        return fdn_fs_status(errno);
    }
    stream = fdopendir(duplicated);
    if (stream == NULL) {
        status = fdn_fs_status(errno);
        (void)close(duplicated);
        return status;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        const size_t name_length = strlen(entry->d_name);
        const size_t separator = prefix_length == 0 ? 0 : 1;
        size_t path_length;
        char *path;
        struct stat info;
        uint32_t kind;
        bool executable;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!fdn_utf8_valid(entry->d_name, name_length) ||
            prefix_length > SIZE_MAX - separator ||
            prefix_length + separator > SIZE_MAX - name_length) {
            status = 6;
            break;
        }
        path_length = prefix_length + separator + name_length;
        if (path_length > tree->max_path_length) {
            status = 5;
            break;
        }
        path = fdn_alloc(path_length + 1);
        if (prefix_length != 0) {
            (void)memcpy(path, prefix, prefix_length);
            path[prefix_length] = '/';
        }
        (void)memcpy(path + prefix_length + separator, entry->d_name,
                     name_length);
        path[path_length] = '\0';
        if (fstatat(directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            status = fdn_fs_status(errno);
            fdn_dealloc(path);
            break;
        }
        if (S_ISREG(info.st_mode)) {
            kind = 1;
        } else if (S_ISDIR(info.st_mode)) {
            kind = 2;
        } else if (S_ISLNK(info.st_mode)) {
            kind = 3;
        } else {
            kind = 4;
        }
        executable = (info.st_mode & S_IXUSR) != 0;
        status = fdn_fs_tree_append(tree, path, path_length, kind,
                                    executable,
                                    kind != 1 || info.st_size < 0
                                        ? 0
                                        : (uint64_t)info.st_size);
        if (status == 0 && kind == 2) {
            int flags = O_RDONLY | O_DIRECTORY;
            int child;
#if defined(O_CLOEXEC)
            flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
            flags |= O_NOFOLLOW;
#endif
            child = openat(directory, entry->d_name, flags);
            if (child < 0) {
                status = errno == ELOOP ? 3 : fdn_fs_status(errno);
            } else {
                status = fdn_posix_tree_collect(tree, child, path,
                                                path_length, depth + 1);
                (void)close(child);
            }
        }
        fdn_dealloc(path);
        if (status != 0) {
            break;
        }
        errno = 0;
    }
    if (status == 0 && errno != 0) {
        status = fdn_fs_status(errno);
    }
    if (closedir(stream) != 0 && status == 0) {
        status = fdn_fs_status(errno);
    }
    return status;
}

static int fdn_posix_open_relative_file(int root, const fdn_string *path) {
    char *copy;
    char *current_component;
    int current;
    if (!fdn_fs_relative_path_valid(path) || path->length == SIZE_MAX) {
        errno = EINVAL;
        return -1;
    }
    copy = fdn_alloc(path->length + 1);
    (void)memcpy(copy, path->data, path->length);
    copy[path->length] = '\0';
    current = dup(root);
    if (current < 0) {
        fdn_dealloc(copy);
        return -1;
    }
    current_component = copy;
    while (current_component != NULL) {
        char *separator = strchr(current_component, '/');
        int flags = O_RDONLY;
        int next;
        if (separator != NULL) {
            *separator = '\0';
            flags |= O_DIRECTORY;
        }
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        next = openat(current, current_component, flags);
        if (next < 0) {
            const int open_error = errno;
            (void)close(current);
            fdn_dealloc(copy);
            errno = open_error;
            return -1;
        }
        (void)close(current);
        current = next;
        current_component = separator == NULL ? NULL : separator + 1;
    }
    fdn_dealloc(copy);
    return current;
}

static int32_t fdn_posix_open_directory_path(int root, const char *path,
                                             int create, int *result) {
    char *copy;
    char *component;
    int current;
    if (result == NULL) {
        return 4;
    }
    *result = -1;
    if (path == NULL || path[0] == '\0') {
        current = dup(root);
        if (current < 0) {
            return fdn_fs_status(errno);
        }
        *result = current;
        return 0;
    }
    copy = fdn_alloc(strlen(path) + 1);
    (void)strcpy(copy, path);
    current = dup(root);
    if (current < 0) {
        fdn_dealloc(copy);
        return fdn_fs_status(errno);
    }
    component = copy;
    while (component != NULL) {
        char *separator = strchr(component, '/');
        struct stat info;
        int flags = O_RDONLY | O_DIRECTORY;
        int next;
        if (separator != NULL) {
            *separator = '\0';
        }
        if (create != 0 && mkdirat(current, component, S_IRWXU | S_IRGRP |
                                                        S_IXGRP | S_IROTH |
                                                        S_IXOTH) != 0 &&
            errno != EEXIST) {
            const int32_t status = fdn_fs_status(errno);
            (void)close(current);
            fdn_dealloc(copy);
            return status;
        }
        if (fstatat(current, component, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            const int stat_error = errno;
            const int32_t status = fdn_fs_status(stat_error);
            (void)close(current);
            fdn_dealloc(copy);
            return status;
        }
        if (!S_ISDIR(info.st_mode)) {
            (void)close(current);
            fdn_dealloc(copy);
            return 3;
        }
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        next = openat(current, component, flags);
        if (next < 0) {
            const int open_error = errno;
            const int32_t status = open_error == ELOOP
                                       ? 3
                                       : fdn_fs_status(open_error);
            (void)close(current);
            fdn_dealloc(copy);
            return status;
        }
        (void)close(current);
        current = next;
        component = separator == NULL ? NULL : separator + 1;
    }
    fdn_dealloc(copy);
    *result = current;
    return 0;
}

static int32_t fdn_posix_read_file(int file, uint64_t max_length,
                                   uint64_t *bytes_handle) {
    uint8_t *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const size_t limit = max_length > SIZE_MAX ? SIZE_MAX : (size_t)max_length;
    struct stat info;
    int32_t status = 0;
    if (bytes_handle == NULL) {
        return 4;
    }
    *bytes_handle = 0;
    if (fstat(file, &info) != 0) {
        return fdn_fs_status(errno);
    }
    if (!S_ISREG(info.st_mode)) {
        return 3;
    }
    if (info.st_size < 0 || (uint64_t)info.st_size > max_length) {
        return 5;
    }
    capacity = (size_t)info.st_size;
    if (capacity != 0) {
        data = fdn_alloc(capacity);
    }
    for (;;) {
        ssize_t count;
        if (length == capacity) {
            uint8_t probe;
            count = read(file, &probe, 1);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0) {
                status = fdn_fs_status(errno);
            } else if (count > 0) {
                status = 5;
            }
            break;
        }
        count = read(file, data + length, capacity - length);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            status = fdn_fs_status(errno);
            break;
        }
        if (count == 0) {
            break;
        }
        length += (size_t)count;
    }
    if (status != 0) {
        if (data != NULL) {
            (void)memset(data, 0, capacity);
            fdn_dealloc(data);
        }
        return status;
    }
    if (length > limit || fdn_bytes_adopt(data, length, capacity, bytes_handle) != 0) {
        if (data != NULL) {
            (void)memset(data, 0, capacity);
            fdn_dealloc(data);
        }
        return 5;
    }
    return 0;
}

static int32_t fdn_posix_write_root_file(fdn_fs_root *root,
                                         const fdn_string *path,
                                         uint64_t bytes_handle,
                                         uint32_t permissions) {
    static atomic_uint_fast64_t sequence = ATOMIC_VAR_INIT(0);
    const uint8_t *data;
    size_t length;
    char *copy;
    char *name;
    char *separator;
    int directory = -1;
    int file = -1;
    char temporary[96];
    size_t offset = 0;
    int32_t status;
    struct stat existing;
    if (!fdn_fs_relative_path_valid(path) || permissions > 511 ||
        fdn_bytes_view(bytes_handle, &data, &length) != 0) {
        return 3;
    }
    temporary[0] = '\0';
    copy = fdn_alloc(path->length + 1);
    (void)memcpy(copy, path->data, path->length);
    copy[path->length] = '\0';
    separator = strrchr(copy, '/');
    if (separator == NULL) {
        name = copy;
        status = fdn_posix_open_directory_path(root->descriptor, "", 1,
                                               &directory);
    } else {
        *separator = '\0';
        name = separator + 1;
        status = fdn_posix_open_directory_path(root->descriptor, copy, 1,
                                               &directory);
    }
    if (status != 0) {
        fdn_dealloc(copy);
        return status;
    }
    {
        const int existing_result =
            fstatat(directory, name, &existing, AT_SYMLINK_NOFOLLOW);
        const int existing_error = errno;
        if (existing_result == 0 && !S_ISREG(existing.st_mode)) {
            status = 3;
            goto cleanup;
        }
        if (existing_result != 0 && existing_error != ENOENT) {
            status = fdn_fs_status(existing_error);
            goto cleanup;
        }
    }
    for (;;) {
        const uint64_t current = atomic_fetch_add_explicit(
            &sequence, 1, memory_order_relaxed);
        const int written = snprintf(temporary, sizeof(temporary),
                                     ".foundation-%ld-%" PRIu64 ".tmp",
                                     (long)getpid(), current);
        int flags = O_WRONLY | O_CREAT | O_EXCL;
        if (written < 0 || (size_t)written >= sizeof(temporary)) {
            status = 4;
            goto cleanup;
        }
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        file = openat(directory, temporary, flags,
                      S_IRUSR | S_IWUSR);
        if (file >= 0) {
            break;
        }
        if (errno != EEXIST) {
            status = fdn_fs_status(errno);
            goto cleanup;
        }
    }
    while (offset < length) {
        const size_t remaining = length - offset;
        const size_t requested = remaining > (size_t)INT_MAX
                                     ? (size_t)INT_MAX
                                     : remaining;
        const ssize_t written = write(file, data + offset, requested);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            status = written < 0 ? fdn_fs_status(errno) : 4;
            goto cleanup;
        }
        offset += (size_t)written;
    }
    if (fchmod(file, (mode_t)permissions) != 0) {
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    if (fsync(file) != 0) {
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    if (close(file) != 0) {
        file = -1;
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    file = -1;
    if (renameat(directory, temporary, directory, name) != 0 ||
        (fsync(directory) != 0
#if defined(__APPLE__)
         && errno != EINVAL && errno != ENOTSUP
#endif
         )) {
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    temporary[0] = '\0';
    status = 0;

cleanup:
    if (file >= 0) {
        (void)close(file);
    }
    if (temporary[0] != '\0') {
        (void)unlinkat(directory, temporary, 0);
    }
    (void)close(directory);
    fdn_dealloc(copy);
    return status;
}
#endif

int32_t foundation_runtime_fs_tree_open(const fdn_string *path,
                                        uint64_t max_entries,
                                        uint64_t max_path_length,
                                        uint64_t *handle) {
    fdn_fs_tree *tree;
    if (handle == NULL) {
        return 4;
    }
    *handle = 0;
    if (max_entries == 0 || max_entries > SIZE_MAX ||
        max_path_length == 0 || max_path_length > SIZE_MAX) {
        return 5;
    }
    tree = fdn_alloc(sizeof(*tree));
    (void)memset(tree, 0, sizeof(*tree));
    tree->max_entries = (size_t)max_entries;
    tree->max_path_length = (size_t)max_path_length;
#if defined(_WIN32)
    {
        int32_t status;
        tree->root = fdn_windows_path(path);
        tree->root_handle = INVALID_HANDLE_VALUE;
        if (tree->root == NULL) {
            fdn_fs_tree_release(tree);
            return 3;
        }
        tree->root_handle = fdn_windows_open_directory_guard(tree->root);
        if (tree->root_handle == INVALID_HANDLE_VALUE) {
            status = fdn_windows_fs_status(GetLastError());
            fdn_fs_tree_release(tree);
            return status;
        }
        status = fdn_windows_tree_collect(tree, tree->root, "", 0, 0);
        if (status != 0) {
            fdn_fs_tree_release(tree);
            return status;
        }
    }
#else
    {
        char *native_path = fdn_native_path(path);
        int flags = O_RDONLY | O_DIRECTORY;
        int32_t status;
        struct stat info;
        tree->root = -1;
        if (native_path == NULL) {
            fdn_fs_tree_release(tree);
            return 3;
        }
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        tree->root = open(native_path, flags);
        fdn_dealloc(native_path);
        if (tree->root < 0) {
            status = errno == ELOOP ? 3 : fdn_fs_status(errno);
            fdn_fs_tree_release(tree);
            return status;
        }
        if (fstat(tree->root, &info) != 0 || !S_ISDIR(info.st_mode)) {
            fdn_fs_tree_release(tree);
            return 3;
        }
        status = fdn_posix_tree_collect(tree, tree->root, "", 0, 0);
        if (status != 0) {
            fdn_fs_tree_release(tree);
            return status;
        }
    }
#endif
    if (tree->count > 1) {
        qsort(tree->entries, tree->count, sizeof(*tree->entries),
              fdn_fs_tree_entry_compare);
    }
    *handle = (uint64_t)(uintptr_t)tree;
    fdn_handle_count_add(&fdn_live_directory_count);
    return 0;
}

int32_t foundation_runtime_fs_tree_next(uint64_t handle, fdn_string *path,
                                        uint32_t *kind, bool *executable,
                                        uint64_t *size) {
    fdn_fs_tree *tree = (fdn_fs_tree *)(uintptr_t)handle;
    const fdn_fs_tree_entry *entry;
    if (tree == NULL || path == NULL || kind == NULL || executable == NULL ||
        size == NULL) {
        return 4;
    }
    fdn_string_drop(path);
    *path = fdn_string_static("", 0);
    *kind = 0;
    *executable = false;
    *size = 0;
    if (tree->cursor >= tree->count) {
        return 0;
    }
    entry = &tree->entries[tree->cursor++];
    *path = fdn_string_copy(entry->path, entry->path_length);
    *kind = entry->kind;
    *executable = entry->executable;
    *size = entry->size;
    return 1;
}

int32_t foundation_runtime_fs_tree_read(uint64_t handle,
                                        const fdn_string *relative_path,
                                        uint64_t max_length,
                                        uint64_t *bytes_handle) {
    fdn_fs_tree *tree = (fdn_fs_tree *)(uintptr_t)handle;
    if (tree == NULL || bytes_handle == NULL) {
        return 4;
    }
    *bytes_handle = 0;
#if defined(_WIN32)
    return fdn_windows_read_tree_file(tree->root, relative_path,
                                      max_length, bytes_handle);
#else
    {
        const int file = fdn_posix_open_relative_file(tree->root,
                                                      relative_path);
        int32_t status;
        if (file < 0) {
            return errno == ELOOP ? 3 : fdn_fs_status(errno);
        }
        status = fdn_posix_read_file(file, max_length, bytes_handle);
        if (close(file) != 0 && status == 0) {
            status = fdn_fs_status(errno);
        }
        return status;
    }
#endif
}

int32_t foundation_runtime_fs_tree_close(uint64_t *handle) {
    fdn_fs_tree *tree;
    if (handle == NULL) {
        return 4;
    }
    tree = (fdn_fs_tree *)(uintptr_t)*handle;
    *handle = 0;
    if (tree == NULL) {
        return 0;
    }
    fdn_fs_tree_release(tree);
    fdn_handle_count_remove(&fdn_live_directory_count);
    return 0;
}

int32_t foundation_runtime_fs_root_open(const fdn_string *path,
                                        uint64_t *handle) {
    fdn_fs_root *root;
    int32_t status;
    if (handle == NULL) {
        return 4;
    }
    *handle = 0;
    status = foundation_runtime_fs_create_private_directory(path);
    if (status != 0) {
        return status;
    }
    root = fdn_alloc(sizeof(*root));
#if defined(_WIN32)
    root->path = fdn_windows_path(path);
    root->directory = INVALID_HANDLE_VALUE;
    if (root->path == NULL) {
        fdn_dealloc(root);
        return 3;
    }
    root->directory = fdn_windows_open_directory_guard(root->path);
    if (root->directory == INVALID_HANDLE_VALUE) {
        status = fdn_windows_fs_status(GetLastError());
        fdn_dealloc(root->path);
        fdn_dealloc(root);
        return status;
    }
#else
    {
        char *native_path = fdn_native_path(path);
        int flags = O_RDONLY | O_DIRECTORY;
        struct stat info;
        if (native_path == NULL) {
            fdn_dealloc(root);
            return 3;
        }
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        root->descriptor = open(native_path, flags);
        fdn_dealloc(native_path);
        if (root->descriptor < 0) {
            status = errno == ELOOP ? 3 : fdn_fs_status(errno);
            fdn_dealloc(root);
            return status;
        }
        if (fstat(root->descriptor, &info) != 0 || !S_ISDIR(info.st_mode)) {
            (void)close(root->descriptor);
            fdn_dealloc(root);
            return 3;
        }
    }
#endif
    *handle = (uint64_t)(uintptr_t)root;
    fdn_handle_count_add(&fdn_live_directory_count);
    return 0;
}

int32_t foundation_runtime_fs_root_create_directory(
    uint64_t handle, const fdn_string *relative_path) {
    fdn_fs_root *root = (fdn_fs_root *)(uintptr_t)handle;
    if (root == NULL || !fdn_fs_relative_path_valid(relative_path)) {
        return 3;
    }
#if defined(_WIN32)
    return fdn_windows_create_root_directory(root, relative_path);
#else
    {
        char *path = fdn_native_path(relative_path);
        int directory = -1;
        int32_t status;
        if (path == NULL) {
            return 3;
        }
        status = fdn_posix_open_directory_path(root->descriptor, path, 1,
                                               &directory);
        fdn_dealloc(path);
        if (directory >= 0) {
            if (fchmod(directory, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
                                      S_IXOTH) != 0 && status == 0) {
                status = fdn_fs_status(errno);
            }
            (void)close(directory);
        }
        return status;
    }
#endif
}

int32_t foundation_runtime_fs_root_write_file(
    uint64_t handle, const fdn_string *relative_path, uint64_t bytes_handle,
    uint32_t permissions) {
    fdn_fs_root *root = (fdn_fs_root *)(uintptr_t)handle;
    if (root == NULL) {
        return 4;
    }
#if defined(_WIN32)
    return fdn_windows_write_root_file(root, relative_path, bytes_handle,
                                       permissions);
#else
    return fdn_posix_write_root_file(root, relative_path, bytes_handle,
                                     permissions);
#endif
}

int32_t foundation_runtime_fs_root_close(uint64_t *handle) {
    fdn_fs_root *root;
    int32_t status = 0;
    if (handle == NULL) {
        return 4;
    }
    root = (fdn_fs_root *)(uintptr_t)*handle;
    *handle = 0;
    if (root == NULL) {
        return 0;
    }
#if defined(_WIN32)
    if (CloseHandle(root->directory) == 0) {
        status = fdn_windows_fs_status(GetLastError());
    }
    fdn_dealloc(root->path);
#else
    if (close(root->descriptor) != 0) {
        status = fdn_fs_status(errno);
    }
#endif
    fdn_dealloc(root);
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

int32_t foundation_runtime_fs_read_private_text_limited(const fdn_string *path,
                                                        uint64_t max_length,
                                                        fdn_string *result) {
    char chunk[4096];
    char *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const size_t limit = max_length > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)max_length;
    if (result == NULL) {
        fdn_panic_cstr("private file text output is null");
    }
    fdn_string_drop(result);
    *result = fdn_string_static("", 0);
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_windows_path(path);
        HANDLE file;
        BY_HANDLE_FILE_INFORMATION info;
        if (native_path == NULL) {
            return 3;
        }
        file = CreateFileW(native_path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        fdn_dealloc(native_path);
        if (file == INVALID_HANDLE_VALUE) {
            return fdn_windows_fs_status(GetLastError());
        }
        if (GetFileInformationByHandle(file, &info) == 0 ||
            (info.dwFileAttributes &
             (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
            (void)CloseHandle(file);
            return 3;
        }
        for (;;) {
            DWORD count = 0;
            if (ReadFile(file, chunk, (DWORD)sizeof(chunk), &count, NULL) == 0) {
                fdn_dealloc(data);
                (void)CloseHandle(file);
                return 4;
            }
            if (count == 0) {
                break;
            }
            if ((size_t)count > limit - length) {
                fdn_dealloc(data);
                (void)CloseHandle(file);
                return 5;
            }
            {
                const size_t required = length + (size_t)count;
                if (required > capacity) {
                    size_t next_capacity = capacity == 0 ? sizeof(chunk) : capacity;
                    char *grown;
                    if (next_capacity > limit) {
                        next_capacity = limit;
                    }
                    while (next_capacity < required) {
                        next_capacity = next_capacity > limit / 2 ? limit : next_capacity * 2;
                    }
                    grown = fdn_alloc(next_capacity);
                    if (length != 0) {
                        (void)memcpy(grown, data, length);
                    }
                    fdn_dealloc(data);
                    data = grown;
                    capacity = next_capacity;
                }
                (void)memcpy(data + length, chunk, (size_t)count);
                length = required;
            }
        }
        if (CloseHandle(file) == 0) {
            fdn_dealloc(data);
            return 4;
        }
    }
#else
    {
        char *native_path = fdn_native_path(path);
        int flags = O_RDONLY;
        int file;
        struct stat info;
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        if (native_path == NULL) {
            return 3;
        }
        file = open(native_path, flags);
        fdn_dealloc(native_path);
        if (file < 0) {
#if defined(ELOOP)
            if (errno == ELOOP) {
                return 3;
            }
#endif
            return fdn_fs_status(errno);
        }
        if (fstat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
            (void)close(file);
            return 3;
        }
        for (;;) {
            const ssize_t count = read(file, chunk, sizeof(chunk));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fdn_dealloc(data);
                (void)close(file);
                return 4;
            }
            if (count == 0) {
                break;
            }
            if ((size_t)count > limit - length) {
                fdn_dealloc(data);
                (void)close(file);
                return 5;
            }
            {
                const size_t required = length + (size_t)count;
                if (required > capacity) {
                    size_t next_capacity = capacity == 0 ? sizeof(chunk) : capacity;
                    char *grown;
                    if (next_capacity > limit) {
                        next_capacity = limit;
                    }
                    while (next_capacity < required) {
                        next_capacity = next_capacity > limit / 2 ? limit : next_capacity * 2;
                    }
                    grown = fdn_alloc(next_capacity);
                    if (length != 0) {
                        (void)memcpy(grown, data, length);
                    }
                    fdn_dealloc(data);
                    data = grown;
                    capacity = next_capacity;
                }
                (void)memcpy(data + length, chunk, (size_t)count);
                length = required;
            }
        }
        if (close(file) != 0) {
            fdn_dealloc(data);
            return 4;
        }
    }
#endif
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

#if defined(_WIN32)
static int32_t fdn_windows_create_private_directory_path(wchar_t *path) {
    size_t offset;
    const size_t length = wcslen(path);
    size_t first = 0;
    if (length == 0) {
        return 3;
    }
    if (length >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        first = 3;
    } else if (length >= 2 &&
               (path[0] == L'\\' || path[0] == L'/') &&
               (path[1] == L'\\' || path[1] == L'/')) {
        unsigned int separators = 0;
        first = 2;
        while (first < length && separators < 2) {
            if (path[first] == L'\\' || path[first] == L'/') {
                ++separators;
            }
            ++first;
        }
    }
    for (offset = first; offset <= length; ++offset) {
        wchar_t saved;
        DWORD attributes;
        if (offset != length && path[offset] != L'\\' && path[offset] != L'/') {
            continue;
        }
        if (offset == 0 || (offset > 0 &&
                            (path[offset - 1] == L'\\' || path[offset - 1] == L'/'))) {
            continue;
        }
        saved = path[offset];
        path[offset] = L'\0';
        if (CreateDirectoryW(path, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
            const int32_t status = fdn_windows_fs_status(GetLastError());
            path[offset] = saved;
            return status;
        }
        attributes = GetFileAttributesW(path);
        path[offset] = saved;
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return 3;
        }
    }
    return 0;
}
#else
static int32_t fdn_posix_create_private_directory_path(char *path) {
    size_t offset;
    size_t length = strlen(path);
    const size_t first = length != 0 && path[0] == '/' ? 1 : 0;
    if (length == 0) {
        return 3;
    }
    while (length > first && path[length - 1] == '/') {
        path[--length] = '\0';
    }
    if (first == 1 && length == 1) {
        return 3;
    }
    for (offset = first; offset <= length; ++offset) {
        char saved;
        struct stat info;
        int created = 0;
        if (offset != length && path[offset] != '/') {
            continue;
        }
        if (offset == 0 || (offset > 0 && path[offset - 1] == '/')) {
            continue;
        }
        saved = path[offset];
        path[offset] = '\0';
        if (mkdir(path, S_IRWXU) == 0) {
            created = 1;
        } else if (errno != EEXIST) {
            const int32_t mkdir_status = fdn_fs_status(errno);
            path[offset] = saved;
            return mkdir_status;
        }
        if (stat(path, &info) != 0) {
            const int32_t stat_status = fdn_fs_status(errno);
            path[offset] = saved;
            return stat_status;
        }
        if (!S_ISDIR(info.st_mode)) {
            path[offset] = saved;
            return 3;
        }
        if ((created != 0 || offset == length) && chmod(path, S_IRWXU) != 0) {
            const int32_t chmod_status = fdn_fs_status(errno);
            path[offset] = saved;
            return chmod_status;
        }
        path[offset] = saved;
    }
    return 0;
}
#endif

int32_t foundation_runtime_fs_create_private_directory(const fdn_string *path) {
#if defined(_WIN32)
    wchar_t *native_path = fdn_windows_path(path);
    int32_t status;
    if (native_path == NULL) {
        return 3;
    }
    status = fdn_windows_create_private_directory_path(native_path);
    fdn_dealloc(native_path);
    return status;
#else
    char *native_path = fdn_native_path(path);
    int32_t status;
    if (native_path == NULL) {
        return 3;
    }
    status = fdn_posix_create_private_directory_path(native_path);
    fdn_dealloc(native_path);
    return status;
#endif
}

#if defined(_WIN32)
static wchar_t *fdn_windows_parent_path(const wchar_t *path) {
    const wchar_t *backslash = wcsrchr(path, L'\\');
    const wchar_t *slash = wcsrchr(path, L'/');
    const wchar_t *separator = backslash;
    wchar_t *result;
    size_t length;
    if (separator == NULL || (slash != NULL && slash > separator)) {
        separator = slash;
    }
    if (separator == NULL) {
        result = fdn_alloc(2 * sizeof(*result));
        result[0] = L'.';
        result[1] = L'\0';
        return result;
    }
    if (separator == path) {
        length = 1;
    } else if (separator == path + 2 && path[1] == L':') {
        length = 3;
    } else {
        length = (size_t)(separator - path);
    }
    result = fdn_alloc((length + 1) * sizeof(*result));
    (void)memcpy(result, path, length * sizeof(*result));
    result[length] = L'\0';
    return result;
}

static int32_t fdn_windows_write_private_text_atomic(const wchar_t *path,
                                                     const fdn_string *value) {
    wchar_t *directory = fdn_windows_parent_path(path);
    wchar_t temporary[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t offset = 0;
    int32_t status = 4;
    temporary[0] = L'\0';
    if (GetTempFileNameW(directory, L"fdn", 0, temporary) == 0) {
        status = fdn_windows_fs_status(GetLastError());
        goto cleanup;
    }
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        status = fdn_windows_fs_status(GetLastError());
        goto cleanup;
    }
    while (offset < value->length) {
        const size_t remaining = value->length - offset;
        const DWORD requested =
            remaining > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)remaining;
        DWORD written = 0;
        if (WriteFile(file, value->data + offset, requested, &written, NULL) == 0 ||
            written == 0) {
            status = fdn_windows_fs_status(GetLastError());
            goto cleanup;
        }
        offset += (size_t)written;
    }
    if (FlushFileBuffers(file) == 0) {
        status = 4;
        goto cleanup;
    }
    if (CloseHandle(file) == 0) {
        file = INVALID_HANDLE_VALUE;
        status = 4;
        goto cleanup;
    }
    file = INVALID_HANDLE_VALUE;
    if (MoveFileExW(temporary, path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        status = fdn_windows_fs_status(GetLastError());
        goto cleanup;
    }
    temporary[0] = L'\0';
    status = 0;

cleanup:
    if (file != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(file);
    }
    if (temporary[0] != L'\0') {
        (void)DeleteFileW(temporary);
    }
    fdn_dealloc(directory);
    return status;
}
#else
static char *fdn_posix_parent_path(const char *path) {
    const char *separator = strrchr(path, '/');
    char *result;
    size_t length;
    if (separator == NULL) {
        result = fdn_alloc(2);
        result[0] = '.';
        result[1] = '\0';
        return result;
    }
    length = separator == path ? 1 : (size_t)(separator - path);
    result = fdn_alloc(length + 1);
    (void)memcpy(result, path, length);
    result[length] = '\0';
    return result;
}

static int32_t fdn_posix_write_private_text_atomic(const char *path,
                                                   const fdn_string *value) {
    char *directory = fdn_posix_parent_path(path);
    const size_t directory_length = strlen(directory);
    static const char suffix[] = "/.fn.XXXXXX";
    char *temporary;
    int file;
    size_t offset = 0;
    int32_t status = 4;
    if (directory_length > SIZE_MAX - sizeof(suffix)) {
        fdn_dealloc(directory);
        return 3;
    }
    temporary = fdn_alloc(directory_length + sizeof(suffix));
    (void)memcpy(temporary, directory, directory_length);
    (void)memcpy(temporary + directory_length, suffix, sizeof(suffix));
    fdn_dealloc(directory);
    file = mkstemp(temporary);
    if (file < 0) {
        status = fdn_fs_status(errno);
        fdn_dealloc(temporary);
        return status;
    }
    if (fchmod(file, S_IRUSR | S_IWUSR) != 0) {
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    while (offset < value->length) {
        const size_t remaining = value->length - offset;
        const size_t requested = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        const ssize_t written = write(file, value->data + offset, requested);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = fdn_fs_status(errno);
            goto cleanup;
        }
        if (written == 0) {
            goto cleanup;
        }
        offset += (size_t)written;
    }
    if (fsync(file) != 0 || close(file) != 0) {
        file = -1;
        goto cleanup;
    }
    file = -1;
    if (rename(temporary, path) != 0) {
        status = fdn_fs_status(errno);
        goto cleanup;
    }
    status = 0;

cleanup:
    if (file >= 0) {
        (void)close(file);
    }
    if (status != 0) {
        (void)unlink(temporary);
    }
    fdn_dealloc(temporary);
    return status;
}
#endif

int32_t foundation_runtime_fs_write_private_text_atomic(const fdn_string *path,
                                                        const fdn_string *value,
                                                        uint64_t max_length) {
    if (value == NULL || (value->data == NULL && value->length != 0)) {
        return 4;
    }
    if (value->length > max_length) {
        return 5;
    }
#if defined(_WIN32)
    {
        wchar_t *native_path = fdn_windows_path(path);
        int32_t status;
        if (native_path == NULL) {
            return 3;
        }
        status = fdn_windows_write_private_text_atomic(native_path, value);
        fdn_dealloc(native_path);
        return status;
    }
#else
    {
        char *native_path = fdn_native_path(path);
        int32_t status;
        if (native_path == NULL) {
            return 3;
        }
        status = fdn_posix_write_private_text_atomic(native_path, value);
        fdn_dealloc(native_path);
        return status;
    }
#endif
}

int32_t foundation_runtime_fs_delete_private_file(const fdn_string *path) {
#if defined(_WIN32)
    wchar_t *native_path = fdn_windows_path(path);
    DWORD attributes;
    int32_t status;
    if (native_path == NULL) {
        return 3;
    }
    attributes = GetFileAttributesW(native_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        status = fdn_windows_fs_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        fdn_dealloc(native_path);
        return 3;
    }
    if (DeleteFileW(native_path) == 0) {
        status = fdn_windows_fs_status(GetLastError());
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#else
    char *native_path = fdn_native_path(path);
    struct stat info;
    int32_t status;
    if (native_path == NULL) {
        return 3;
    }
    if (lstat(native_path, &info) != 0) {
        status = fdn_fs_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    if (!S_ISREG(info.st_mode)) {
        fdn_dealloc(native_path);
        return 3;
    }
    if (unlink(native_path) != 0) {
        status = fdn_fs_status(errno);
        fdn_dealloc(native_path);
        return status;
    }
    fdn_dealloc(native_path);
    return 0;
#endif
}

uint64_t foundation_runtime_fs_live_directories(void) {
    return fdn_handle_count_read(&fdn_live_directory_count);
}
