#include <stddef.h>

#include "lib/runtime/memory.h"

void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
);
void *memset(void *destination, int byte, size_t length);

void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
)
{
    return micros_runtime_memcpy(destination, source, length);
}

void *memset(void *destination, int byte, size_t length)
{
    return micros_runtime_memset(destination, byte, length);
}
