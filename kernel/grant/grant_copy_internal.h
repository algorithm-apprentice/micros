#ifndef MICROS_KERNEL_GRANT_COPY_INTERNAL_H
#define MICROS_KERNEL_GRANT_COPY_INTERNAL_H

#include "micros/grant_copy.h"

struct micros_tty_device_authority;

enum micros_grant_error grant_copy_plan_range(
    struct micros_process_handle process,
    uintptr_t address,
    size_t length,
    uint32_t required_permission,
    const struct micros_tty_device_authority *device_authority,
    struct micros_grant_copy_range_plan *plan
);

#endif
