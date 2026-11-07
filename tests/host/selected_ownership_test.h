#ifndef MICROS_SELECTED_OWNERSHIP_TEST_H
#define MICROS_SELECTED_OWNERSHIP_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_objects.h"

_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "MEM03 native bindings require LP64");

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#endif

struct micros_frame_allocator *micros_selected_native_allocator(void);
struct micros_frame_ownership *micros_selected_native_ownership(void);
void micros_selected_native_reset(void);
bool micros_selected_native_irq_enabled(void);

#endif
