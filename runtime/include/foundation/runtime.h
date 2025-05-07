#ifndef FOUNDATION_RUNTIME_H
#define FOUNDATION_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fdn_println(const char *value);
#ifdef __cplusplus
[[noreturn]] void fdn_invalid_enum_tag(void);
#else
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
