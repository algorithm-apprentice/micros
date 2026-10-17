#include "kernel/pm/pm_control_internal.h"

#include "kernel/vm/vm_handoff_core.h"
#include "micros/scheduler_core.h"
#include "micros/spawn.h"

enum micros_pm_control_error micros_pm_control_activate_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token,
    struct micros_pm_control_activate_plan *plan
)
{
    struct micros_pm_control_activate_plan candidate;
    enum micros_pm_control_error shape_error;

    if (
        plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_activate_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    shape_error = micros_pm_control_validate_prepared_shape(
        state,
        objects,
        registry,
        vm,
        reservation_token,
        load_token,
        mapping_generation,
        prepared_token
    );
    if (shape_error != MICROS_PM_CONTROL_OK) {
        return shape_error;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    candidate.process = state->prepared.process;
    candidate.thread = state->prepared.thread;
    candidate.hart = state->prepared.hart;
    candidate.endpoint = state->prepared.endpoint;
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_activate_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_activate_plan *plan
)
{
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];
    struct micros_hart *hart = &objects->harts[plan->hart.slot];
    struct micros_thread_handle *head =
        &hart->ready_head[MICROS_APPLICATION_SCHEDULER_PRIORITY];
    struct micros_thread_handle *tail =
        &hart->ready_tail[MICROS_APPLICATION_SCHEDULER_PRIORITY];

    registry->endpoints[plan->process.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
    vm->mappings[plan->process.slot].phase =
        MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT;
    thread->runtime_flags = 0;
    thread->ready_linked = true;
    if (micros_pm_control_thread_handle_is_zero(*head)) {
        *head = plan->thread;
    } else {
        objects->threads[tail->slot].ready_next = plan->thread;
    }
    *tail = plan->thread;
    state->active_transaction = 0;
    state->reserved_process =
        (struct micros_process_handle){0, 0};
    micros_pm_control_clear_bytes(&state->prepared, sizeof(state->prepared));
}
