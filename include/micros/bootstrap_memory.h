#ifndef MICROS_BOOTSTRAP_MEMORY_H
#define MICROS_BOOTSTRAP_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/fdt.h"
#include "micros/frame_allocator.h"

enum micros_frame_allocator_error micros_bootstrap_memory_initialize(
    const struct micros_fdt_memory_map *memory_map
);

const struct micros_frame_allocator *micros_bootstrap_frame_allocator(void);

#ifdef MICROS_BUILD_FRAME_ALLOCATOR_TEST
bool micros_bootstrap_memory_run_self_test(
    const struct micros_fdt_memory_map *memory_map
);
#endif

#endif
