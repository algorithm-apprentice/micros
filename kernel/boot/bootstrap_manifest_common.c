#include "kernel/boot/bootstrap_manifest_internal.h"

void bootstrap_manifest_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

void bootstrap_manifest_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool bootstrap_manifest_bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

bool bootstrap_manifest_pointer_is_aligned(
    const void *pointer,
    size_t alignment
)
{
    return (uintptr_t)pointer % alignment == 0;
}

bool bootstrap_manifest_fixed_name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    bool found_nul = false;
    size_t index;

    if (name[0] == '\0') {
        return false;
    }
    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (name[index] == '\0') {
            found_nul = true;
        } else if (found_nul) {
            return false;
        }
    }
    return found_nul;
}

bool bootstrap_manifest_fixed_names_equal(
    const char left[MICROS_BOOTSTRAP_NAME_SIZE],
    const char right[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    size_t index;

    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool bootstrap_manifest_fixed_name_matches_literal(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *literal
)
{
    size_t index = 0;

    while (literal[index] != '\0') {
        if (
            index >= MICROS_BOOTSTRAP_NAME_SIZE
            || name[index] != literal[index]
        ) {
            return false;
        }
        ++index;
    }
    for (; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (name[index] != '\0') {
            return false;
        }
    }
    return true;
}
