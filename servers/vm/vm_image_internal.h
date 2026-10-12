#ifndef MICROS_SERVERS_VM_IMAGE_INTERNAL_H
#define MICROS_SERVERS_VM_IMAGE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servers/vm/vm_image.h"

uint16_t vm_image_read_u16_le(const uint8_t *bytes);
uint32_t vm_image_read_u32_le(const uint8_t *bytes);
uint64_t vm_image_read_u64_le(const uint8_t *bytes);
void vm_image_write_u64_le(uint8_t *bytes, uint64_t value);
void vm_image_zero_bytes(uint8_t *bytes, size_t size);
void vm_image_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
);
bool vm_image_bytes_are_zero(const uint8_t *bytes, size_t size);
bool vm_image_add_u64(uint64_t left, uint64_t right, uint64_t *sum);
bool vm_image_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
);
bool vm_image_address_ranges_overlap(
    uint64_t left_start,
    uint64_t left_end,
    uint64_t right_start,
    uint64_t right_end
);

#endif
