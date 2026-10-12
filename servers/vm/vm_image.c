#include "servers/vm/vm_image_internal.h"

#include <stdbool.h>
#include <stdint.h>

uint16_t vm_image_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)((uint16_t)bytes[1] << 8)
    );
}

uint32_t vm_image_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t vm_image_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

void vm_image_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

void vm_image_zero_bytes(uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void vm_image_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

bool vm_image_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

bool vm_image_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

bool vm_image_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (
        left_size > UINTPTR_MAX - left_start
        || right_size > UINTPTR_MAX - right_start
    ) {
        return true;
    }
    left_end = left_start + left_size;
    right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

bool vm_image_address_ranges_overlap(
    uint64_t left_start,
    uint64_t left_end,
    uint64_t right_start,
    uint64_t right_end
)
{
    return left_start < right_end && right_start < left_end;
}
