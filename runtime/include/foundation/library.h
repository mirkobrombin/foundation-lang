#ifndef FOUNDATION_LIBRARY_H
#define FOUNDATION_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#define FOUNDATION_LIBRARY_ABI_MAJOR UINT32_C(1)
#define FOUNDATION_LIBRARY_ABI_MINOR UINT32_C(0)

#ifdef __cplusplus
extern "C" {
#endif

#if defined(FOUNDATION_LIBRARY_STATIC)
#define FOUNDATION_LIBRARY_RUNTIME_API
#elif defined(_WIN32)
#define FOUNDATION_LIBRARY_RUNTIME_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define FOUNDATION_LIBRARY_RUNTIME_API __attribute__((visibility("default")))
#else
#define FOUNDATION_LIBRARY_RUNTIME_API
#endif

#ifndef FOUNDATION_FDN_STRING_DEFINED
#define FOUNDATION_FDN_STRING_DEFINED
typedef struct fdn_string {
    const char *data;
    size_t length;
    uint8_t owned;
} fdn_string;

static inline fdn_string fdn_string_static(const char *data, size_t length) {
    fdn_string value = {data, length, 0};
    return value;
}
#define FOUNDATION_FDN_STRING_STATIC_DEFINED
#endif

FOUNDATION_LIBRARY_RUNTIME_API void *fdn_alloc(size_t size);
FOUNDATION_LIBRARY_RUNTIME_API void fdn_dealloc(void *value);
FOUNDATION_LIBRARY_RUNTIME_API void fdn_string_drop(fdn_string *value);
#ifdef __cplusplus
[[noreturn]] FOUNDATION_LIBRARY_RUNTIME_API void fdn_panic(fdn_string message);
[[noreturn]] FOUNDATION_LIBRARY_RUNTIME_API void fdn_panic_cstr(const char *message);
#else
_Noreturn FOUNDATION_LIBRARY_RUNTIME_API void fdn_panic(fdn_string message);
_Noreturn FOUNDATION_LIBRARY_RUNTIME_API void fdn_panic_cstr(const char *message);
#endif

#undef FOUNDATION_LIBRARY_RUNTIME_API

#ifdef __cplusplus
}
#endif

#endif
