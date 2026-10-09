#ifndef MICROS_KERNEL_PM_CONTROL_RUNTIME_H
#define MICROS_KERNEL_PM_CONTROL_RUNTIME_H

#include "kernel/bootstrap_control_internal.h"
#include "kernel/pm_control_core.h"

enum micros_pm_control_error micros_pm_control_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry
);

enum micros_pm_control_error micros_pm_control_runtime_reset(void);

const struct micros_pm_control_state *micros_pm_control_runtime_state(
    void
);

struct micros_pm_control_state *
micros_pm_control_runtime_authoritative_state(void);

enum micros_pm_control_error micros_pm_control_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

#endif
