#ifndef MICROS_RUNTIME_MEMORY_H
#define MICROS_RUNTIME_MEMORY_H

#include <stddef.h>

void *micros_runtime_memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
);

void *micros_runtime_memset(
    void *destination,
    int byte,
    size_t length
);

#endif
