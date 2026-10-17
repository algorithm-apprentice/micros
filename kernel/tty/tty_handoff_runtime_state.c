#include "kernel/tty/tty_handoff_runtime_internal.h"

#include <stddef.h>
#include <stdint.h>

static struct tty_handoff_context context_state;

struct tty_handoff_context *tty_handoff_context(void)
{
    return &context_state;
}

void tty_handoff_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void tty_handoff_copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}
