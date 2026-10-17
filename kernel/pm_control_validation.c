#include "kernel/pm_control_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/scheduler_core.h"
#include "micros/spawn.h"

#define MICROS_PM_USER_SSTATUS_UBE (UINT64_C(1) << 6)
#define MICROS_PM_USER_SSTATUS_VS (UINT64_C(3) << 9)
#define MICROS_PM_USER_SSTATUS_FS (UINT64_C(3) << 13)
#define MICROS_PM_USER_SSTATUS_XS (UINT64_C(3) << 15)
#define MICROS_PM_USER_SSTATUS_MXR (UINT64_C(1) << 19)
#define MICROS_PM_USER_SSTATUS_UXL_MASK (UINT64_C(3) << 32)
#define MICROS_PM_USER_SSTATUS_UXL_64 (UINT64_C(2) << 32)
#define MICROS_PM_USER_SSTATUS_SD (UINT64_C(1) << 63)
#define MICROS_PM_USER_SSTATUS_CONTROL_MASK \
    ( \
        MICROS_RISCV_SSTATUS_SIE \
        | MICROS_RISCV_SSTATUS_SPIE \
        | MICROS_RISCV_SSTATUS_SPP \
        | MICROS_PM_USER_SSTATUS_UBE \
        | MICROS_PM_USER_SSTATUS_VS \
        | MICROS_PM_USER_SSTATUS_FS \
        | MICROS_PM_USER_SSTATUS_XS \
        | MICROS_RISCV_SSTATUS_SUM \
        | MICROS_PM_USER_SSTATUS_MXR \
    )

static bool prepared_context_matches(
    const struct micros_user_context *context,
    const struct micros_vm_kernel_mapping_record *record,
    uint64_t initial_sstatus
)
{
    struct micros_user_context remaining;

    if (
        context == NULL
        || record == NULL
        || context->sp != record->stack_pointer
        || context->sstatus != initial_sstatus
        || context->sepc != record->entry
    ) {
        return false;
    }
    micros_pm_control_copy_bytes(
        &remaining,
        context,
        sizeof(remaining)
    );
    remaining.sp = 0;
    remaining.sstatus = 0;
    remaining.sepc = 0;
    return micros_pm_control_bytes_are_zero(
        &remaining,
        sizeof(remaining)
    );
}

static bool prepared_status_is_canonical(uint64_t status)
{
    return (
        (status & MICROS_PM_USER_SSTATUS_UXL_MASK)
            == MICROS_PM_USER_SSTATUS_UXL_64
        && (
            status
            & (
                MICROS_PM_USER_SSTATUS_CONTROL_MASK
                | MICROS_PM_USER_SSTATUS_SD
            )
        ) == MICROS_RISCV_SSTATUS_SPIE
    );
}

static bool prepared_state_is_zero(
    const struct micros_pm_control_state *state
)
{
    return micros_pm_control_bytes_are_zero(
        &state->prepared,
        sizeof(state->prepared)
    );
}

static bool exit_state_is_zero(
    const struct micros_pm_control_state *state
)
{
    return micros_pm_control_bytes_are_zero(
        &state->exit,
        sizeof(state->exit)
    );
}

enum micros_pm_control_error micros_pm_control_state_validate(
    const struct micros_pm_control_state *state
)
{
    if (
        state == NULL
        || !micros_pm_control_pointer_is_aligned(
            state,
            _Alignof(struct micros_pm_control_state)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic != MICROS_PM_CONTROL_MAGIC
        || state->service_id != 3
        || state->process.slot != 2
        || state->profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || state->reserved != 0
        || !micros_pm_control_endpoint_matches_process(
            state->endpoint,
            state->process
        )
        || !micros_pm_control_thread_handle_is_valid(
            state->thread
        )
        || state->exit.phase > MICROS_PM_CONTROL_EXIT_DETACHED
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->exit.phase == MICROS_PM_CONTROL_EXIT_NONE) {
        if (!exit_state_is_zero(state)) {
            return MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
    } else if (
        state->exit.reserved != 0
        || state->exit.reserved_tail != 0
        || state->exit.token == 0
        || state->exit.token != state->last_exit_token
        || state->exit.load_token == 0
        || state->exit.mapping_generation == 0
        || !micros_pm_control_process_handle_is_valid(
            state->exit.process
        )
        || micros_pm_control_process_handles_equal(
            state->exit.process,
            state->process
        )
        || !micros_pm_control_thread_handle_is_valid(
            state->exit.thread
        )
        || !micros_pm_control_endpoint_matches_process(
            state->exit.endpoint,
            state->exit.process
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction == 0) {
        if (
            !micros_pm_control_process_handle_is_zero(
                state->reserved_process
            )
            || !prepared_state_is_zero(state)
        ) {
            return MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
    } else if (
        state->exit.phase != MICROS_PM_CONTROL_EXIT_NONE
        || state->active_transaction != state->last_transaction
        || !micros_pm_control_process_handle_is_valid(
            state->reserved_process
        )
        || micros_pm_control_process_handles_equal(
            state->reserved_process,
            state->process
        )
        || (
            state->prepared.token == 0
            && !prepared_state_is_zero(state)
        )
        || (
            state->prepared.token != 0
            && (
                state->prepared.token
                    != state->last_prepared_token
                || state->prepared.reservation_token
                    != state->active_transaction
                || state->prepared.load_token == 0
                || state->prepared.mapping_generation == 0
                || !prepared_status_is_canonical(
                    state->prepared.initial_sstatus
                )
                || !micros_pm_control_process_handles_equal(
                    state->prepared.process,
                    state->reserved_process
                )
                || !micros_pm_control_thread_handle_is_valid(
                    state->prepared.thread
                )
                || !micros_pm_control_hart_handle_is_valid(
                    state->prepared.hart
                )
                || !micros_pm_control_endpoint_matches_process(
                    state->prepared.endpoint,
                    state->prepared.process
                )
                || state->prepared.reserved != 0
            )
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}

static enum micros_pm_control_error validate_stopped_objects(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_thread *thread;

    if (
        process->live_thread_count != 1
        || process->address_space_root == 0
        || process->primary_endpoint != state->exit.endpoint
        || process->privilege_profile
            != MICROS_PRIVILEGE_PROFILE_APPLICATION
        || !process->endpoint_lifecycle_consumed
        || micros_thread_resolve(
            objects,
            state->exit.thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_pm_control_process_handles_equal(
            thread->owner,
            state->exit.process
        )
        || !thread->context_attached
        || thread->runtime_flags
            != (
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        || !thread->scheduler_assigned
        || !thread->scheduler_preemptible
        || thread->scheduler_priority
            != MICROS_APPLICATION_SCHEDULER_PRIORITY
        || thread->quantum_counter_ticks
            != MICROS_APPLICATION_SCHEDULER_QUANTUM
        || thread->ready_linked
        || !micros_pm_control_thread_handle_is_zero(
            thread->ready_next
        )
        || thread->ipc_queue_kind != MICROS_IPC_QUEUE_NONE
        || thread->ipc_reply_token == 0
        || thread->ipc_reply_callee != state->endpoint
        || thread->ipc_receive_buffer == 0
        || thread->ipc_receive_buffer % 8 != 0
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}

static enum micros_pm_control_error validate_detached_objects(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_thread *released =
        &objects->threads[state->exit.thread.slot];

    return (
        process->live_thread_count == 0
        && process->primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && process->endpoint_lifecycle_consumed
        && released->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        && released->generation == state->exit.thread.generation
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

static enum micros_pm_control_error validate_prepared_objects(
    const struct micros_pm_control_state *state,
    const struct micros_process *process,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_thread *thread;

    if (
        process->live_thread_count != 1
        || process->address_space_root == 0
        || process->primary_endpoint != state->prepared.endpoint
        || process->privilege_profile
            != MICROS_PRIVILEGE_PROFILE_APPLICATION
        || !process->endpoint_lifecycle_consumed
        || micros_thread_resolve(
            objects,
            state->prepared.thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_pm_control_process_handles_equal(
            thread->owner,
            state->prepared.process
        )
        || !thread->context_attached
        || thread->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || !thread->scheduler_assigned
        || !thread->scheduler_preemptible
        || thread->scheduler_priority
            != MICROS_APPLICATION_SCHEDULER_PRIORITY
        || !micros_pm_control_hart_handles_equal(
            thread->scheduler_hart,
            state->prepared.hart
        )
        || thread->quantum_counter_ticks
            != MICROS_APPLICATION_SCHEDULER_QUANTUM
        || thread->remaining_counter_ticks
            != MICROS_APPLICATION_SCHEDULER_QUANTUM
        || thread->ready_linked
        || !micros_pm_control_thread_handle_is_zero(
            thread->ready_next
        )
        || !micros_thread_ipc_state_is_clear(thread)
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}

enum micros_pm_control_error micros_pm_control_state_validate_objects(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_process *process;

    if (
        state == NULL
        || objects == NULL
        || !micros_pm_control_pointer_is_aligned(
            objects,
            _Alignof(struct micros_kernel_objects)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate(state)
            != MICROS_PM_CONTROL_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction == 0) {
        if (state->exit.phase == MICROS_PM_CONTROL_EXIT_NONE) {
            return MICROS_PM_CONTROL_OK;
        }
        if (
            micros_process_resolve(
                objects,
                state->exit.process,
                &process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_PM_CONTROL_ERROR_INVARIANT;
        }
        if (state->exit.phase == MICROS_PM_CONTROL_EXIT_DETACHED) {
            return validate_detached_objects(
                state,
                process,
                objects
            );
        }
        return validate_stopped_objects(state, process, objects);
    }
    if (
        micros_process_resolve(
            objects,
            state->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->prepared.token == 0) {
        return micros_pm_control_process_is_empty(process)
            ? MICROS_PM_CONTROL_OK
            : MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return validate_prepared_objects(state, process, objects);
}

bool micros_pm_control_application_profile_is_exact(
    const struct micros_endpoint_registry *registry
)
{
    const struct micros_privilege_profile *profile;

    return (
        micros_privilege_profile_resolve(
            registry,
            MICROS_PRIVILEGE_PROFILE_APPLICATION,
            &profile
        ) == MICROS_ENDPOINT_OK
        && profile->id == MICROS_PRIVILEGE_PROFILE_APPLICATION
        && profile->operations == MICROS_PRIVILEGE_OPERATION_CALL
        && profile->call_targets
            == (
                (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
                )
            )
        && profile->send_targets == 0
        && profile->notify_targets == 0
        && profile->kernel_operations == 0
    );
}

enum micros_pm_control_error micros_pm_control_validate_prepared_shape(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
)
{
    const struct micros_vm_kernel_mapping_record *record;
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

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
        prepared_token == 0
        || state->prepared.token != prepared_token
        || state->prepared.reservation_token != reservation_token
        || state->prepared.load_token != load_token
        || state->prepared.mapping_generation != mapping_generation
    ) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (
        vm->load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || vm->load.process_reservation_token != reservation_token
        || vm->load.load_token != load_token
        || !micros_pm_control_process_handles_equal(
            vm->load.process,
            state->prepared.process
        )
        || micros_endpoint_resolve_internal(
            registry,
            objects,
            state->prepared.endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || endpoint->state != MICROS_ENDPOINT_STATE_RESERVED
        || !micros_pm_control_process_handles_equal(
            endpoint->owner,
            state->prepared.process
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    record = &vm->mappings[state->prepared.process.slot];
    return (
        record->phase == MICROS_VM_KERNEL_MAPPING_PREPARED
        && record->mapping_generation == mapping_generation
        && record->load_token == load_token
        && micros_pm_control_process_handles_equal(
            record->process,
            state->prepared.process
        )
        && micros_thread_resolve(
            objects,
            state->prepared.thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && prepared_context_matches(
            &thread->user_context,
            record,
            state->prepared.initial_sstatus
        )
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}
