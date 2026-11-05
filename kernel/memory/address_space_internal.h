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
    uint16_t table_range_indices[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];
    uint16_t level1_table_ids[MICROS_SV39_TABLE_ENTRY_COUNT];
    uint16_t level0_table_ids[MICROS_SV39_TABLE_ENTRY_COUNT][MICROS_SV39_TABLE_ENTRY_COUNT];
    struct micros_kernel_address_space_report report;
};

struct micros_frame_allocator;
struct micros_frame_ownership;

bool micros_kernel_address_space_table_frame_is_valid(
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index,
    uint16_t range_index,
    uint64_t physical_address
);

bool micros_kernel_address_space_allocator_is_pristine(
    const struct micros_frame_allocator *allocator
);

bool micros_kernel_address_space_candidate_is_valid(
    const struct micros_kernel_address_space_private_state *state,
    const struct micros_frame_allocator *allocator
);

#endif
