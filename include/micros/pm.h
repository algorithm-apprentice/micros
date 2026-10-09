#ifndef MICROS_PM_H
#define MICROS_PM_H

#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_objects.h"

enum {
    MICROS_PM_PROTOCOL_VERSION = 1,
    MICROS_PM_PROCESS_CAPACITY = MICROS_PROCESS_CAPACITY,
    MICROS_PM_RESERVATION_VERSION = 1,
    MICROS_PM_RESERVATION_SIZE = 32,
};

#define MICROS_PM_MESSAGE_EXIT UINT32_C(0x00010001)
#define MICROS_PM_MESSAGE_WAIT UINT32_C(0x00010002)
#define MICROS_PM_MESSAGE_RESULT UINT32_C(0x00010003)
#define MICROS_PM_WAIT_NOHANG UINT32_C(0x00000001)
#define MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED UINT64_C(0x1)

enum micros_pm_result {
    MICROS_PM_RESULT_OK = 0,
    MICROS_PM_RESULT_BAD_TYPE = -1,
    MICROS_PM_RESULT_BAD_VERSION = -2,
    MICROS_PM_RESULT_MALFORMED = -3,
    MICROS_PM_RESULT_CALLER = -4,
    MICROS_PM_RESULT_STATE = -5,
    MICROS_PM_RESULT_BUSY = -6,
    MICROS_PM_RESULT_NO_CHILD = -7,
};

enum micros_pm_exit_kind {
    MICROS_PM_EXIT_NONE = 0,
    MICROS_PM_EXIT_NORMAL = 1,
    MICROS_PM_EXIT_FAULT = 2,
};

struct micros_pm_process_handle {
    uint16_t slot;
    uint16_t reserved;
    uint32_t generation;
};

struct micros_pm_reservation_result {
    uint32_t version;
    uint32_t size;
    uint64_t transaction;
    uint64_t reserved[2];
};

_Static_assert(
    sizeof(struct micros_pm_process_handle) == 8,
    "PM process handle ABI changed"
);
_Static_assert(
    offsetof(struct micros_pm_process_handle, slot) == 0
        && offsetof(struct micros_pm_process_handle, reserved) == 2
        && offsetof(struct micros_pm_process_handle, generation) == 4,
    "PM process handle offsets changed"
);
_Static_assert(
    sizeof(struct micros_pm_reservation_result)
        == MICROS_PM_RESERVATION_SIZE,
    "PM reservation result ABI changed"
);
_Static_assert(
    offsetof(struct micros_pm_reservation_result, version) == 0
        && offsetof(struct micros_pm_reservation_result, size) == 4
        && offsetof(
            struct micros_pm_reservation_result,
            transaction
        ) == 8
        && offsetof(
            struct micros_pm_reservation_result,
            reserved
        ) == 16,
    "PM reservation result offsets changed"
);

#endif
