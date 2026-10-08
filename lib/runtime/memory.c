#include "lib/runtime/memory.h"

#include <stddef.h>

void *micros_runtime_memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
)
{
    volatile unsigned char *target = destination;
    const volatile unsigned char *input = source;
    size_t index;

    for (index = 0; index < length; ++index) {
        target[index] = input[index];
    }
    return destination;
}

void *micros_runtime_memset(
    void *destination,
    int byte,
    size_t length
)
{
    volatile unsigned char *target = destination;
    unsigned char value = (unsigned char)byte;
    size_t index;

    for (index = 0; index < length; ++index) {
        target[index] = value;
    }
    return destination;
}
