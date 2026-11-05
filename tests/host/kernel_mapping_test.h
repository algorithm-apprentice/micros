#ifndef MICROS_KERNEL_MAPPING_TEST_H
#define MICROS_KERNEL_MAPPING_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/memory/address_space_internal.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"

#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS)
#if MICROS_TEST_KERNEL_MAPPING_COUNTS != 1
#error "MEM02 observations must be undefined or equal to one"
#endif
#include "tests/work/work_counts.h"
#endif

struct micros_frame_allocator *micros_kernel_mapping_native_allocator(void);
struct micros_frame_ownership *micros_kernel_mapping_native_ownership(void);
void micros_kernel_mapping_native_ready(bool ready);
void micros_kernel_mapping_native_irq(bool enabled);
bool micros_kernel_mapping_native_irq_enabled(void);

struct micros_kernel_address_space_private_state *
micros_kernel_mapping_native_state(void);
const uint64_t *micros_kernel_mapping_native_linker_bounds(void);
void micros_kernel_mapping_native_bounds(const uint64_t bounds[4]);
struct micros_kernel_page_table *micros_kernel_mapping_native_table_at(
    uint64_t physical_address
);

enum micros_kernel_address_space_error micros_kernel_mapping_native_construct(void);
enum micros_kernel_address_space_error micros_kernel_mapping_native_allocate_table(
    uint64_t *physical_address
);
enum micros_kernel_address_space_error micros_kernel_mapping_native_map_range(
    uint64_t start,
    uint64_t end,
    uint32_t permissions
);
bool micros_kernel_mapping_native_validate(void);
void micros_kernel_mapping_native_prepare_tree(void);
bool micros_kernel_mapping_native_tree(void);
bool micros_kernel_mapping_native_coverage(void);
bool micros_kernel_mapping_native_lookup(
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
);

#endif
