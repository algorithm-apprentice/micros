#include "servers/pm/pm_coordinator.h"
#include "servers/pm/pm_internal.h"

#define MICROS_PM_COORDINATOR_INITIALIZATION_MAGIC UINT64_C(0x504d434f4f524431)

static bool coordinator_bindings_are_valid(
    const struct micros_pm_coordinator_bindings *bindings
)
{
    return (
        bindings->reserved == 0
        && pm_common_endpoint_is_concrete(bindings->self)
        && pm_common_endpoint_is_concrete(bindings->vm)
        && pm_common_endpoint_is_concrete(bindings->vfs)
        && bindings->self != bindings->vm
        && bindings->self != bindings->vfs
        && bindings->vm != bindings->vfs
    );
}

enum micros_pm_coordinator_error micros_pm_coordinator_initialize(
    struct micros_pm_coordinator *coordinator,
    struct micros_pm_table *table,
    const struct micros_pm_coordinator_bindings *bindings
)
{
    struct micros_pm_coordinator_bindings captured_bindings;

    if (
        coordinator == NULL || table == NULL || bindings == NULL
        || !coordinator_bindings_are_valid(bindings)
    ) {
        return MICROS_PM_COORDINATOR_ERROR_ARGUMENT;
    }
    if (micros_pm_table_validate(table) != MICROS_PM_MODEL_OK) {
        return MICROS_PM_COORDINATOR_ERROR_INVARIANT;
    }
    if (
        coordinator->initialization_magic != 0
        || table->init_attempted || table->last_pid != 0
    ) {
        return MICROS_PM_COORDINATOR_ERROR_STATE;
    }
    captured_bindings = *bindings;
    pm_common_zero_bytes(coordinator, sizeof(*coordinator));
    coordinator->table = table;
    coordinator->bindings = captured_bindings;
    coordinator->initialization_magic = MICROS_PM_COORDINATOR_INITIALIZATION_MAGIC;
    return MICROS_PM_COORDINATOR_OK;
}
