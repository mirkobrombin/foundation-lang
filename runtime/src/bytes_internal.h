#ifndef FOUNDATION_RUNTIME_BYTES_INTERNAL_H
#define FOUNDATION_RUNTIME_BYTES_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct fdn_bytes {
    uint8_t *data;
    size_t length;
    size_t capacity;
} fdn_bytes;

int32_t fdn_bytes_adopt(uint8_t *data, size_t length, size_t capacity,
                        uint64_t *result);
int32_t fdn_bytes_view(uint64_t handle, const uint8_t **data, size_t *length);

#endif
