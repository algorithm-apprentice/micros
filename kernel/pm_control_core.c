#include "kernel/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "kernel/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/scheduler_core.h"
#include "micros/spawn.h"

bool micros_pm_control_bytes_are_zero(
    const void *storage,
    size_t size
)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void micros_pm_control_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_pm_control_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool micros_pm_control_pointer_is_aligned(
    const void *pointer,
    size_t alignment
)
{
    return (uintptr_t)pointer % alignment == 0;
}

bool micros_pm_control_process_handle_is_valid(
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool micros_pm_control_process_handle_is_zero(
    struct micros_process_handle process
)
{
    return process.slot == 0 && process.generation == 0;
}

bool micros_pm_control_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_pm_control_hart_handles_equal(
    struct micros_hart_handle left,
    struct micros_hart_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_pm_control_thread_handle_is_valid(
    struct micros_thread_handle thread
)
{
    return (
        thread.slot < MICROS_THREAD_CAPACITY
        && thread.generation != 0
    );
}

bool micros_pm_control_thread_handle_is_zero(
    struct micros_thread_handle thread
)
{
    return thread.slot == 0 && thread.generation == 0;
}

bool micros_pm_control_hart_handle_is_valid(
    struct micros_hart_handle hart
)
{
    return (
        hart.slot < MICROS_HART_CAPACITY
        && hart.generation != 0
    );
}

bool micros_pm_control_endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
)
{
    return (
        micros_pm_control_process_handle_is_valid(process)
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

bool micros_pm_control_process_is_empty(
    const struct micros_process *process
)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

enum micros_pm_control_error micros_pm_control_state_prepare(
    struct micros_pm_control_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread
)
{
    struct micros_pm_control_state candidate;

    if (
        state == NULL
        || !micros_pm_control_pointer_is_aligned(
            state,
            _Alignof(struct micros_pm_control_state)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (!micros_pm_control_bytes_are_zero(state, sizeof(*state))) {
        return MICROS_PM_CONTROL_ERROR_STORAGE;
    }
    if (
        service_id != 3
        || process.slot != 2
        || profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || !micros_pm_control_endpoint_matches_process(endpoint, process)
        || !micros_pm_control_thread_handle_is_valid(thread)
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    candidate.initialization_magic = MICROS_PM_CONTROL_MAGIC;
    candidate.service_id = service_id;
    candidate.endpoint = endpoint;
    candidate.profile_id = profile_id;
    candidate.process.slot = process.slot;
    candidate.process.generation = process.generation;
    candidate.thread.slot = thread.slot;
    candidate.thread.generation = thread.generation;
    micros_pm_control_copy_bytes(state, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}









static bool running_application_resolve(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint32_t endpoint_value,
    struct micros_process_handle *process_handle,
    struct micros_thread_handle *thread_handle,
    uint64_t *load_token,
    uint64_t *mapping_generation
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_process *process;
    const struct micros_vm_kernel_mapping_record *record;
    const struct micros_thread *thread = NULL;
    size_t index;

    if (
        state->active_transaction != 0
        || state->prepared.token != 0
        || state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE
        || endpoint_value == MICROS_ENDPOINT_NONE
        || endpoint_value == MICROS_ENDPOINT_ANY
        || micros_endpoint_resolve_active(
            registry,
            objects,
            endpoint_value,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_pm_control_process_handles_equal(endpoint->owner, state->process)
        || micros_process_resolve(
            objects,
            endpoint->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || process->live_thread_count != 1
        || process->address_space_root == 0
        || process->primary_endpoint != endpoint_value
        || process->privilege_profile
            != MICROS_PRIVILEGE_PROFILE_APPLICATION
        || !process->endpoint_lifecycle_consumed
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || vm->load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    record = &vm->mappings[endpoint->owner.slot];
    if (
        record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVE
        || !micros_pm_control_process_handles_equal(record->process, endpoint->owner)
        || record->root_physical_address
            != process->address_space_root
        || record->load_token == 0
        || record->mapping_generation == 0
        || record->exit_token != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *candidate =
            &objects->threads[index];

        if (
            candidate->slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !micros_pm_control_process_handles_equal(
                candidate->owner,
                endpoint->owner
            )
        ) {
            continue;
        }
        if (thread != NULL) {
            return false;
        }
        thread = candidate;
        thread_handle->slot = (uint16_t)index;
        thread_handle->generation = candidate->generation;
    }
    if (
        thread == NULL
        || !thread->context_attached
        || !thread->scheduler_assigned
        || !thread->scheduler_preemptible
        || thread->scheduler_priority
            != MICROS_APPLICATION_SCHEDULER_PRIORITY
        || thread->scheduler_hart.slot
            != MICROS_APPLICATION_SCHEDULER_HART
        || thread->scheduler_hart.generation
            != objects->harts[
                MICROS_APPLICATION_SCHEDULER_HART
            ].generation
        || thread->quantum_counter_ticks
            != MICROS_APPLICATION_SCHEDULER_QUANTUM
        || thread->runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
        || thread->ready_linked
        || !micros_pm_control_thread_handle_is_zero(thread->ready_next)
        || thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || !micros_pm_control_thread_handle_is_zero(thread->ipc_next)
        || !micros_pm_control_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        || thread->ipc_send_destination != 0
        || thread->ipc_receive_source != 0
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
        || thread->ipc_delivery_pending
        || !micros_pm_control_bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        || thread->ipc_staged_result != MICROS_IPC_OK
        || thread->ipc_reply_token == 0
        || thread->ipc_reply_callee != state->endpoint
    ) {
        return false;
    }
    *process_handle = endpoint->owner;
    *load_token = record->load_token;
    *mapping_generation = record->mapping_generation;
    return true;
}

enum micros_pm_control_error micros_pm_control_exit_begin_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint32_t endpoint,
    struct micros_pm_control_exit_begin_plan *plan
)
{
    struct micros_pm_control_exit_begin_plan candidate;

    if (
        state == NULL
        || objects == NULL
        || registry == NULL
        || vm == NULL
        || plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_exit_begin_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
        || micros_endpoint_registry_validate_objects(registry, objects)
            != MICROS_ENDPOINT_OK
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || micros_scheduler_core_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || !micros_pm_control_application_profile_is_exact(registry)
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        state->last_exit_token == UINT64_MAX
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    micros_pm_control_clear_bytes(&candidate, sizeof(candidate));
    if (
        !running_application_resolve(
            state,
            objects,
            registry,
            vm,
            endpoint,
            &candidate.process,
            &candidate.thread,
            &candidate.load_token,
            &candidate.mapping_generation
        )
        || micros_endpoint_preflight_source_only_for_teardown(
            registry,
            objects,
            endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    candidate.endpoint = endpoint;
    candidate.exit_token = state->last_exit_token + 1;
    candidate.result.version = MICROS_PM_EXIT_BEGIN_VERSION;
    candidate.result.size = MICROS_PM_EXIT_BEGIN_SIZE;
    candidate.result.exit_token = candidate.exit_token;
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_exit_begin_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    const struct micros_pm_control_exit_begin_plan *plan
)
{
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];

    micros_endpoint_commit_source_only_prevalidated(
        registry,
        plan->endpoint
    );
    thread->runtime_flags |= MICROS_THREAD_RTS_INACTIVE;
    state->last_exit_token = plan->exit_token;
    state->exit.phase = MICROS_PM_CONTROL_EXIT_STOPPED;
    state->exit.token = plan->exit_token;
    state->exit.load_token = plan->load_token;
    state->exit.mapping_generation = plan->mapping_generation;
    state->exit.process = plan->process;
    state->exit.thread = plan->thread;
    state->exit.endpoint = plan->endpoint;
}

static enum micros_pm_control_error validate_stopped_exit(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t exit_token,
    uint32_t endpoint_value
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_vm_kernel_mapping_record *record;

    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
        || micros_endpoint_registry_validate_objects(registry, objects)
            != MICROS_ENDPOINT_OK
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
        || micros_scheduler_core_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        state->exit.phase != MICROS_PM_CONTROL_EXIT_STOPPED
        || state->exit.token != exit_token
        || exit_token == 0
        || state->exit.endpoint != endpoint_value
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (
        micros_endpoint_resolve_internal(
            registry,
            objects,
            endpoint_value,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || endpoint->state != MICROS_ENDPOINT_STATE_SOURCE_ONLY
        || !micros_pm_control_process_handles_equal(
            endpoint->owner,
            state->exit.process
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    record = &vm->mappings[state->exit.process.slot];
    return (
        vm->load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && record->phase == MICROS_VM_KERNEL_MAPPING_ACTIVE
        && record->load_token == state->exit.load_token
        && record->mapping_generation
            == state->exit.mapping_generation
        && micros_pm_control_process_handles_equal(
            record->process,
            state->exit.process
        )
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

enum micros_pm_control_error micros_pm_control_exit_detach_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t exit_token,
    uint32_t endpoint,
    struct micros_pm_control_exit_detach_plan *plan
)
{
    struct micros_pm_control_exit_detach_plan candidate;
    uint32_t unused_generation;
    enum micros_kernel_object_error generation_error;
    enum micros_pm_control_error exit_error;

    if (
        state == NULL
        || objects == NULL
        || registry == NULL
        || vm == NULL
        || plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_exit_detach_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    exit_error = validate_stopped_exit(
        state,
        objects,
        registry,
        vm,
        exit_token,
        endpoint
    );
    if (exit_error != MICROS_PM_CONTROL_OK) {
        return exit_error;
    }
    generation_error = micros_thread_next_generation(
        state->exit.thread.generation,
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
    candidate.process = state->exit.process;
    candidate.thread = state->exit.thread;
    candidate.endpoint = state->exit.endpoint;
    candidate.quarantine_thread =
        generation_error
            == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    candidate.exit_token = state->exit.token;
    candidate.load_token = state->exit.load_token;
    candidate.mapping_generation =
        state->exit.mapping_generation;
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_exit_detach_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_exit_detach_plan *plan
)
{
    struct micros_thread *thread =
        &objects->threads[plan->thread.slot];
    struct micros_process *process =
        &objects->processes[plan->process.slot];
    uint32_t generation = thread->generation;

    thread->scheduler_assigned = false;
    thread->scheduler_preemptible = false;
    thread->scheduler_priority = 0;
    thread->scheduler_hart = (struct micros_hart_handle){0, 0};
    thread->quantum_counter_ticks = 0;
    thread->remaining_counter_ticks = 0;
    thread->ready_linked = false;
    thread->ready_next = (struct micros_thread_handle){0, 0};
    thread->context_attached = false;
    thread->kernel_stack_bottom = 0;
    thread->kernel_stack_top = 0;
    micros_pm_control_clear_bytes(&thread->user_context, sizeof(thread->user_context));
    micros_pm_control_clear_bytes(thread, sizeof(*thread));
    thread->slot_state = plan->quarantine_thread
        ? MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        : MICROS_KERNEL_OBJECT_SLOT_FREE;
    thread->generation = generation;
    --process->live_thread_count;
    --objects->live_thread_count;
    (void)micros_vm_load_mark_exit_detached(
        vm,
        plan->process,
        plan->exit_token,
        plan->load_token,
        plan->mapping_generation
    );
    state->exit.phase = MICROS_PM_CONTROL_EXIT_DETACHED;
}

enum micros_pm_control_error
micros_pm_control_release_exited_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_vm_handoff_state *vm,
    uint64_t exit_token,
    struct micros_pm_control_release_exited_plan *plan
)
{
    struct micros_pm_control_release_exited_plan candidate;
    const struct micros_process *process;
    uint32_t unused_generation;
    enum micros_kernel_object_error generation_error;

    if (
        state == NULL
        || objects == NULL
        || vm == NULL
        || plan == NULL
        || !micros_pm_control_pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_release_exited_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
        || micros_vm_handoff_state_validate(vm)
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        state->exit.phase != MICROS_PM_CONTROL_EXIT_DETACHED
        || state->exit.token != exit_token
        || exit_token == 0
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (
        micros_process_resolve(
            objects,
            state->exit.process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || process->live_thread_count != 0
        || process->address_space_root != 0
        || process->primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || process->privilege_profile != 0
        || !process->endpoint_lifecycle_consumed
        || !micros_pm_control_bytes_are_zero(
            &vm->mappings[state->exit.process.slot],
            sizeof(vm->mappings[state->exit.process.slot])
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    generation_error = micros_process_next_generation(
        state->exit.process.slot,
        state->exit.process.generation,
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
    candidate.process = state->exit.process;
    candidate.quarantine_process =
        generation_error
            == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    micros_pm_control_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_release_exited_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_release_exited_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];

    process->slot_state = plan->quarantine_process
        ? MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        : MICROS_KERNEL_OBJECT_SLOT_FREE;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    --objects->live_process_count;
    micros_pm_control_clear_bytes(&state->exit, sizeof(state->exit));
}
