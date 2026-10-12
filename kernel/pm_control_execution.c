#include "kernel/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "kernel/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/scheduler_core.h"
#include "micros/spawn.h"

static enum micros_pm_control_error validate_frozen_mapping(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    const struct micros_vm_kernel_mapping_record *record;
    const struct micros_process *process;

    if (
        state->prepared.token != 0
        || reservation_token == 0
        || load_token == 0
        || mapping_generation == 0
        || state->active_transaction != reservation_token
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (
        vm->load.process_reservation_token
            != state->active_transaction
        || !micros_pm_control_process_handles_equal(
            vm->load.process,
            state->reserved_process
        )
        || micros_process_resolve(
            objects,
            state->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    record = &vm->mappings[state->reserved_process.slot];
    if (
        vm->load.load_token != load_token
        || record->mapping_generation != mapping_generation
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    return (
        record->phase == MICROS_VM_KERNEL_MAPPING_FROZEN
        && record->load_token == vm->load.load_token
        && micros_pm_control_process_handles_equal(
            record->process,
            state->reserved_process
        )
        && record->root_physical_address != 0
        && process->live_thread_count == 0
        && process->address_space_root
            == record->root_physical_address
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

static bool prepare_environment_is_valid(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm
)
{
    return (
        micros_pm_control_state_validate(state)
            == MICROS_PM_CONTROL_OK
        && micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            registry,
            objects
        ) == MICROS_ENDPOINT_OK
        && micros_vm_handoff_state_validate(vm)
            == MICROS_VM_HANDOFF_OK
        && micros_scheduler_core_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_pm_control_application_profile_is_exact(
            registry
        )
    );
}

static bool prepare_runtime_resources_are_valid(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry
)
{
    const struct micros_endpoint_record *endpoint;

    if (
        state->last_prepared_token == UINT64_MAX
        || objects->registered_hart_count != 1
        || objects->harts[
            MICROS_APPLICATION_SCHEDULER_HART
        ].slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || objects->harts[
            MICROS_APPLICATION_SCHEDULER_HART
        ].hardware_id != MICROS_APPLICATION_SCHEDULER_HART
    ) {
        return false;
    }
    endpoint = &registry->endpoints[state->reserved_process.slot];
    return (
        endpoint->state == MICROS_ENDPOINT_STATE_FREE
        && micros_pm_control_bytes_are_zero(
            (const unsigned char *)endpoint
                + sizeof(endpoint->state),
            sizeof(*endpoint) - sizeof(endpoint->state)
        )
    );
}

static enum micros_pm_control_error select_prepare_thread(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle *thread
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        enum micros_kernel_object_error generation_error;
        uint32_t generation;

        if (
            objects->threads[index].slot_state
                != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        generation_error = micros_thread_next_generation(
            objects->threads[index].generation,
            &generation
        );
        if (
            generation_error
                == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
        ) {
            continue;
        }
        if (generation_error != MICROS_KERNEL_OBJECT_OK) {
            return MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
        thread->slot = (uint16_t)index;
        thread->generation = generation;
        return MICROS_PM_CONTROL_OK;
    }
    return MICROS_PM_CONTROL_ERROR_CAPACITY;
}

static void build_prepare_candidate(
    const struct micros_pm_control_state *state,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_pm_control_prepare_plan *candidate
)
{
    candidate->process = state->reserved_process;
    candidate->endpoint = (
        candidate->process.generation << MICROS_ENDPOINT_SLOT_BITS
    ) | candidate->process.slot;
    candidate->reservation_token = reservation_token;
    candidate->load_token = load_token;
    candidate->mapping_generation = mapping_generation;
    candidate->prepared_token = state->last_prepared_token + 1;
    candidate->initial_context.sp =
        vm->mappings[candidate->process.slot].stack_pointer;
    candidate->initial_context.sepc =
        vm->mappings[candidate->process.slot].entry;
    candidate->result.version = MICROS_PM_PREPARED_VERSION;
    candidate->result.size = MICROS_PM_PREPARED_SIZE;
    candidate->result.prepared_token =
        candidate->prepared_token;
    candidate->result.endpoint = candidate->endpoint;
}

enum micros_pm_control_error micros_pm_control_prepare_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_pm_control_prepare_plan *plan
)
{
    struct micros_pm_control_prepare_plan candidate;
    enum micros_pm_control_error mapping_error;

    if (
        state == NULL
        || objects == NULL
        || registry == NULL
        || vm == NULL
        || plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_prepare_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (!prepare_environment_is_valid(
        state,
        objects,
        registry,
        vm
    )) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    mapping_error = validate_frozen_mapping(
        state,
        objects,
        vm,
        reservation_token,
        load_token,
        mapping_generation
    );
    if (mapping_error != MICROS_PM_CONTROL_OK) {
        return mapping_error;
    }
    if (!prepare_runtime_resources_are_valid(
        state,
        objects,
        registry
    )) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    mapping_error = select_prepare_thread(
        objects,
        &candidate.thread
    );
    if (mapping_error != MICROS_PM_CONTROL_OK) {
        return mapping_error;
    }
    build_prepare_candidate(
        state,
        vm,
        reservation_token,
        load_token,
        mapping_generation,
        &candidate
    );
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_prepare_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_prepare_plan *plan,
    const struct micros_pm_control_execution_binding *binding
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];
    struct micros_endpoint_record *endpoint =
        &registry->endpoints[plan->process.slot];

    micros_pm_control_clear_bytes(thread, sizeof(*thread));
    thread->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    thread->generation = plan->thread.generation;
    thread->owner = plan->process;
    thread->context_attached = true;
    thread->kernel_stack_bottom = binding->kernel_stack_bottom;
    thread->kernel_stack_top = binding->kernel_stack_top;
    micros_pm_control_copy_bytes(
        &thread->user_context,
        &binding->context,
        sizeof(thread->user_context)
    );
    thread->runtime_flags = MICROS_THREAD_RTS_INACTIVE;
    thread->scheduler_assigned = true;
    thread->scheduler_preemptible = true;
    thread->scheduler_priority =
        MICROS_APPLICATION_SCHEDULER_PRIORITY;
    thread->scheduler_hart = binding->hart;
    thread->quantum_counter_ticks =
        MICROS_APPLICATION_SCHEDULER_QUANTUM;
    thread->remaining_counter_ticks =
        MICROS_APPLICATION_SCHEDULER_QUANTUM;
    ++process->live_thread_count;
    ++objects->live_thread_count;

    micros_pm_control_clear_bytes(endpoint, sizeof(*endpoint));
    endpoint->state = MICROS_ENDPOINT_STATE_RESERVED;
    endpoint->owner = plan->process;
    endpoint->value = plan->endpoint;
    process->primary_endpoint = plan->endpoint;
    process->privilege_profile =
        MICROS_PRIVILEGE_PROFILE_APPLICATION;
    process->endpoint_lifecycle_consumed = true;

    vm->mappings[plan->process.slot].phase =
        MICROS_VM_KERNEL_MAPPING_PREPARED;
    state->last_prepared_token = plan->prepared_token;
    state->prepared.token = plan->prepared_token;
    state->prepared.reservation_token =
        plan->reservation_token;
    state->prepared.load_token = plan->load_token;
    state->prepared.mapping_generation =
        plan->mapping_generation;
    state->prepared.initial_sstatus = binding->context.sstatus;
    state->prepared.process = plan->process;
    state->prepared.thread = plan->thread;
    state->prepared.hart = binding->hart;
    state->prepared.endpoint = plan->endpoint;
}

enum micros_pm_control_error
micros_pm_control_abort_prepared_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token,
    struct micros_pm_control_abort_prepared_plan *plan
)
{
    struct micros_pm_control_abort_prepared_plan candidate;
    enum micros_pm_control_error shape_error;
    uint32_t unused_generation;
    enum micros_kernel_object_error generation_error;

    if (
        plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_abort_prepared_plan)
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
    generation_error = micros_thread_next_generation(
        state->prepared.thread.generation,
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
    candidate.process = state->prepared.process;
    candidate.thread = state->prepared.thread;
    candidate.endpoint = state->prepared.endpoint;
    candidate.quarantine_thread =
        generation_error
            == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_abort_prepared_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_abort_prepared_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];
    uint32_t generation = thread->generation;

    micros_pm_control_clear_bytes(thread, sizeof(*thread));
    thread->slot_state = plan->quarantine_thread
        ? MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        : MICROS_KERNEL_OBJECT_SLOT_FREE;
    thread->generation = generation;
    --process->live_thread_count;
    --objects->live_thread_count;
    micros_pm_control_clear_bytes(
        &registry->endpoints[plan->process.slot],
        sizeof(registry->endpoints[plan->process.slot])
    );
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    vm->mappings[plan->process.slot].phase =
        MICROS_VM_KERNEL_MAPPING_FROZEN;
    micros_pm_control_clear_bytes(&state->prepared, sizeof(state->prepared));
}
