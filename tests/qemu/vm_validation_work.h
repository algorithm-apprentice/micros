#ifndef MICROS_VM_VALIDATION_WORK_H
#define MICROS_VM_VALIDATION_WORK_H

#if defined(MICROS_TEST_VM_COMMAND_COUNTS)
#if MICROS_TEST_VM_COMMAND_COUNTS != 1 \
    || !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1
#error "VM command observations require the test-owned collector"
#endif

#include <stdbool.h>
#include "tests/work/work_counts.h"

#define MICROS_VM01_KERNEL_PRODUCER UINT64_C(0x564d3031544b0001)
#define MICROS_VM01_SERVICE_PRODUCER UINT64_C(0x564d303154530001)
#define MICROS_VM01_KERNEL_OWNER_MASK ((UINT32_C(1) << 17) - 1)
#define MICROS_VM01_SERVICE_OWNER_MASK \
    ((UINT32_C(1) << MICROS_WORK_OWNER_RUNTIME) \
     | (UINT32_C(1) << MICROS_WORK_OWNER_LIB_VM) \
     | (UINT32_C(1) << MICROS_WORK_OWNER_LIB_SPAWN) \
     | (UINT32_C(1) << MICROS_WORK_OWNER_VM_SERVICE))

struct micros_vm01_work_capture {
    struct micros_test_work_snapshot work;
    uint64_t commands[7];
    uint64_t attempts;
    uint32_t complete;
    uint32_t reserved;
    uint32_t started;
    uint32_t active;
    uint32_t invalid;
};

_Static_assert(sizeof(struct micros_vm01_work_capture) <= 4096,
               "VM work capture plus the existing collector fits two pages");

bool micros_vm01_work_active(void);
bool micros_vm01_service_begin(uint32_t command);
bool micros_vm01_service_finish(uint32_t command);

#define MICROS_VM01_WORK_HIT(site, owner, kind, units) \
    do { \
        if (micros_vm01_work_active()) { \
            MICROS_TEST_WORK_HIT(site, owner, kind, units); \
        } \
    } while (false)
#else
#define MICROS_VM01_WORK_HIT(site, owner, kind, units) ((void)0)
#endif

#endif
