#include "kernel/pm/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

enum micros_pm_control_error micros_pm_control_reserve_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    struct micros_pm_control_reserve_plan *plan
)
{
    struct micros_pm_control_reserve_plan candidate;
    size_t index;

    if (
        plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_reserve_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        state->active_transaction != 0
        || state->prepared.token != 0
        || state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (state->last_transaction == UINT64_MAX) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        uint32_t generation;

        if (
            objects->processes[index].slot_state
                != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        if (
            micros_process_next_generation(
                (uint16_t)index,
                objects->processes[index].generation,
                &generation
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            continue;
        }
        candidate.process.slot = (uint16_t)index;
        candidate.process.generation = generation;
        break;
    }
    if (candidate.process.generation == 0) {
        return MICROS_PM_CONTROL_ERROR_CAPACITY;
    }
    candidate.transaction = state->last_transaction + 1;
    candidate.result.version = MICROS_PM_RESERVATION_VERSION;
    candidate.result.size = MICROS_PM_RESERVATION_SIZE;
    candidate.result.transaction = candidate.transaction;
    micros_pm_control_copy_bytes(
        plan,
        &candidate,
        sizeof(candidate)
    );
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_reserve_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_reserve_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];

    process->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    process->generation = plan->process.generation;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    ++objects->live_process_count;
    state->last_transaction = plan->transaction;
    state->active_transaction = plan->transaction;
    state->reserved_process = plan->process;
}

enum micros_pm_control_error micros_pm_control_abort_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    uint64_t transaction,
    struct micros_pm_control_abort_plan *plan
)
{
    struct micros_pm_control_abort_plan candidate;
    uint32_t unused_generation;
    enum micros_kernel_object_error generation_error;

    if (
        transaction == 0
        || plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_abort_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction != transaction) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    generation_error = micros_process_next_generation(
        state->reserved_process.slot,
        state->reserved_process.generation,
        &unused_generation
    );
    if (
        generation_error != MICROS_KERNEL_OBJECT_OK
        && generation_error
            != MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    candidate.process = state->reserved_process;
    candidate.transaction = transaction;
    candidate.quarantine = generation_error
        == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    micros_pm_control_copy_bytes(
        plan,
        &candidate,
        sizeof(candidate)
    );
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_abort_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_abort_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];

    process->slot_state = plan->quarantine
        ? MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        : MICROS_KERNEL_OBJECT_SLOT_FREE;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    --objects->live_process_count;
    state->active_transaction = 0;
    state->reserved_process.slot = 0;
    state->reserved_process.generation = 0;
}
