#ifndef MICROS_COMMON_SCHEDULER_RETURN_TEST_H
#define MICROS_COMMON_SCHEDULER_RETURN_TEST_H

#include <stddef.h>
#include <stdint.h>

#include "micros/scheduler_core.h"

_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "OBJ02 requires LP64");

#if !defined(MICROS_OBJ02_NATIVE_STAGE) \
    || MICROS_OBJ02_NATIVE_STAGE < 0 || MICROS_OBJ02_NATIVE_STAGE > 1
#error "OBJ02 native evidence requires an exact source stage"
#endif

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#define MICROS_OBJ02_OBSERVE_VALUE(site, owner, kind, value) \
    (micros_test_work_record((owner), (kind), 1), (value))
#endif

size_t micros_obj02_return_workspace_size(void);

#endif
