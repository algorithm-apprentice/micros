#ifndef MICROS_KERNEL_PM_CONTROL_RUNTIME_CORE_H
#define MICROS_KERNEL_PM_CONTROL_RUNTIME_CORE_H

#include "kernel/pm_control_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/frame_ownership.h"

enum micros_pm_control_error
micros_pm_control_runtime_validate_reservation(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_state *vm
);

enum micros_pm_control_error
micros_pm_control_runtime_abort_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
);

enum micros_pm_control_error
micros_pm_control_runtime_reserve_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm
);

#endif
