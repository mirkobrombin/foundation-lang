#ifndef FOUNDATION_CORE_INTERNAL_H
#define FOUNDATION_CORE_INTERNAL_H

#include <stdint.h>

#if defined(_WIN32) && !defined(FOUNDATION_FREESTANDING)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef volatile LONG64 fdn_handle_count;
#elif defined(FOUNDATION_FREESTANDING)
#include <stdatomic.h>
/* Pointer width keeps the counters lock-free on 32-bit triples. */
typedef atomic_size_t fdn_handle_count;
#define FDN_HANDLE_COUNT_MAX SIZE_MAX
#else
#include <stdatomic.h>
typedef atomic_uint_fast64_t fdn_handle_count;
#define FDN_HANDLE_COUNT_MAX UINT64_MAX
#endif

/* Counts live runtime handles across every thread or context. Adding past the maximum and
   removing from zero panic. */
void fdn_handle_count_add(fdn_handle_count *count);
void fdn_handle_count_remove(fdn_handle_count *count);
uint64_t fdn_handle_count_read(fdn_handle_count *count);

#endif
