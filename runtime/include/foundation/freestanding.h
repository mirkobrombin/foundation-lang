#ifndef FOUNDATION_FREESTANDING_H
#define FOUNDATION_FREESTANDING_H

#include "foundation/library.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Integrator-owned storage for one logical execution that runs Foundation code. The contents
   are private to the runtime. All-zero is the initial state for every word, including words
   reserved for later minors. */
typedef struct fdn_context {
    void *fdn_reserved[8];
} fdn_context;

#define FDN_CONTEXT_INIT {{0}}

#ifdef __cplusplus
static_assert(sizeof(fdn_context) == 8 * sizeof(void *),
              "fdn_context has the size of eight pointers");
static_assert(alignof(fdn_context) == alignof(void *),
              "fdn_context has the alignment of a pointer");
#else
_Static_assert(sizeof(fdn_context) == 8 * sizeof(void *),
               "fdn_context has the size of eight pointers");
_Static_assert(_Alignof(fdn_context) == _Alignof(void *),
               "fdn_context has the alignment of a pointer");
#endif

/* The innermost active frame of a panicking context. The strings are static and NUL-terminated.
   package_name is NULL for a native boundary frame. */
typedef struct fdn_panic_location {
    const char *package_name;
    const char *function_name;
    const char *source_file;
    uint32_t line;
    uint32_t column;
} fdn_panic_location;

/* Resets a context that has no active Foundation frame, or one whose execution panicked. */
void fdn_context_init(fdn_context *context);

/* The hooks below are defined by the integrator. No hook may call a Foundation export or an
   fdn_ function, except that fdn_hook_panic may call fdn_context_init for another context. */

/* Returns the context of the calling execution. It is called on every Foundation function entry
   and exit, so it must be constant-time and must not block or allocate. */
fdn_context *fdn_hook_context(void);

/* Returns uninitialized storage of at least size bytes, with size at least 1, aligned to
   _Alignof(max_align_t), or NULL when storage is exhausted. */
void *fdn_hook_alloc(size_t size);

/* Releases one non-null pointer returned by fdn_hook_alloc, possibly from another context. */
void fdn_hook_free(void *value);

/* Receives one panic on the panicking context and never returns. message is borrowed and is
   not necessarily NUL-terminated. location is NULL when no frame is active. */
#ifdef __cplusplus
[[noreturn]] void fdn_hook_panic(fdn_string message, const fdn_panic_location *location);
#else
_Noreturn void fdn_hook_panic(fdn_string message, const fdn_panic_location *location);
#endif

/* Receives borrowed print output. It is required only when the library prints. */
void fdn_hook_write(const char *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
