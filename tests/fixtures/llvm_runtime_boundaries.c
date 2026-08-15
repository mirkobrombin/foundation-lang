#include "foundation_abi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int32_t foundation_llvm_sum(const int32_t *values, size_t length) {
    int32_t result = 0;
    for (size_t index = 0; index < length; ++index) {
        result += values[index];
    }
    return result;
}

bool foundation_llvm_ready(bool value) { return value; }
