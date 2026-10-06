#ifndef MICROS_SCHEDULER_CORE_INTERNAL_H
#define MICROS_SCHEDULER_CORE_INTERNAL_H

#include "micros/scheduler_core.h"

enum micros_kernel_object_error micros_kernel_objects_validate_base(
    const struct micros_kernel_objects *objects
);

void micros_scheduler_apply_return_plan(
    struct micros_kernel_objects *objects,
    const struct micros_scheduler_return_plan *plan
);

#endif
