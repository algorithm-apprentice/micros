#ifndef MICROS_OBJECT_HART_VALIDATION_TEST_H
#define MICROS_OBJECT_HART_VALIDATION_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_objects.h"

_Static_assert(
    sizeof(uintptr_t) == sizeof(uint64_t),
    "OBJ01 native evidence requires LP64"
);

#if !defined(MICROS_OBJ01_NATIVE_STAGE) \
    || MICROS_OBJ01_NATIVE_STAGE < 0 || MICROS_OBJ01_NATIVE_STAGE > 1
#error "OBJ01 native evidence requires an exact source stage"
#endif

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#if !defined(MICROS_OBJ01_NATIVE_CHANNEL) \
    || MICROS_OBJ01_NATIVE_CHANNEL < 0 || MICROS_OBJ01_NATIVE_CHANNEL > 1
#error "OBJ01 observations require an exact native channel"
#endif
#define MICROS_OBJ01_OBSERVE_VALUE(site, owner, kind, value) \
    (micros_test_work_record((owner), (kind), 1), (value))
#endif

#endif
