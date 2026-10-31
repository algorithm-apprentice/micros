#ifndef MICROS_TEST_WORK_COUNTS_H
#define MICROS_TEST_WORK_COUNTS_H

#include "micros/work_observation.h"

#if !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1
#error "the work collector is test-only"
#endif

#include <stdint.h>

enum micros_test_work_status {
    MICROS_TEST_WORK_OK,
    MICROS_TEST_WORK_ARGUMENT,
    MICROS_TEST_WORK_STATE,
    MICROS_TEST_WORK_OWNER,
    MICROS_TEST_WORK_KIND,
    MICROS_TEST_WORK_OVERFLOW,
};

#define MICROS_TEST_WORK_OWNER_MASK \
    ((UINT32_C(1) << MICROS_WORK_OWNER_COUNT) - 1)

struct micros_test_work_snapshot {
    uint32_t version;
    uint32_t reserved0;
    uint64_t producer_label;
    uint64_t capture_label;
    uint32_t owner_mask;
    uint32_t reserved1;
    uint64_t counts[MICROS_WORK_OWNER_COUNT][MICROS_WORK_KIND_COUNT];
};

_Static_assert(
    sizeof(struct micros_test_work_snapshot) <= 4096,
    "work snapshots must fit one page"
);

enum micros_test_work_status micros_test_work_begin(
    uint64_t producer_label,
    uint64_t capture_label,
    uint32_t owner_mask
);

enum micros_test_work_status micros_test_work_finish(
    struct micros_test_work_snapshot *output
);

#endif
