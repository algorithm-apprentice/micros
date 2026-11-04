#ifndef MICROS_BOOTSTRAP_ALLOCATION_TEST_H
#define MICROS_BOOTSTRAP_ALLOCATION_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/fdt.h"
#include "micros/frame_allocator.h"

#if defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS)
#if MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS != 1
#error "MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS must be undefined or equal to 1"
#endif
#if !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1
#error "MEM01 observations require the work collector"
#endif
#include "tests/work/work_counts.h"
#endif

#if defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
#if MICROS_BOOTSTRAP_ALLOCATION_BASELINE != 1
#error "the native baseline selector must be undefined or equal to 1"
#endif
#endif

struct micros_frame_allocator *micros_bootstrap_allocation_native_allocator(
    void
);

void micros_bootstrap_allocation_native_bounds(
    uint64_t kernel_start,
    uint64_t kernel_end
);

bool micros_bootstrap_allocation_native_validate(
    const struct micros_fdt_memory_map *memory_map,
    bool require_all_free
);

bool micros_bootstrap_allocation_native_frame_allowed(
    const struct micros_fdt_memory_map *memory_map,
    uint64_t physical_address
);

#endif
