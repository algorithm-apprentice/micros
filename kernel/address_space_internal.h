#ifndef MICROS_KERNEL_ADDRESS_SPACE_INTERNAL_H
#define MICROS_KERNEL_ADDRESS_SPACE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_address_space.h"
#include "micros/sv39.h"

enum {
    MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES = 1024,
};

struct micros_kernel_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

struct micros_kernel_address_space_private_state {
    bool initialized;
    uint64_t root_physical_address;
    uint64_t table_count;
    uint64_t leaf_count;
    uint64_t table_frames[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];
    struct micros_kernel_address_space_report report;
};

struct micros_frame_allocator;

bool micros_kernel_address_space_allocator_is_pristine(
    const struct micros_frame_allocator *allocator
);

bool micros_kernel_address_space_candidate_is_valid(
    const struct micros_kernel_address_space_private_state *state,
    const struct micros_frame_allocator *allocator
);

#endif
