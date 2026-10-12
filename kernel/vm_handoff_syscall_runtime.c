#include "kernel/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/panic.h"

void micros_vm_handoff_syscall_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_vm_handoff_syscall_copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool micros_vm_handoff_syscall_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_vm_handoff_syscall_process_is_empty(const struct micros_process *process)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

_Noreturn void micros_vm_handoff_syscall_panic(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

enum micros_syscall_return micros_vm_handoff_syscall_return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

bool micros_vm_handoff_syscall_map_error(
    enum micros_vm_handoff_error error,
    enum micros_syscall_abi_result *result
)
{
    switch (error) {
    case MICROS_VM_HANDOFF_ERROR_ARGUMENT:
    case MICROS_VM_HANDOFF_ERROR_SUMMARY:
        *result = MICROS_SYSCALL_ABI_ARGUMENT;
        return true;
    case MICROS_VM_HANDOFF_ERROR_STATE:
        *result = MICROS_SYSCALL_ABI_STATE;
        return true;
    default:
        return false;
    }
}

_Noreturn void micros_vm_handoff_syscall_fail(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_vm_handoff_state *state
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            reason,
            state->service_id,
            state->endpoint,
            0
        );
    }
    micros_vm_handoff_syscall_panic(hart, frame, "vm-handoff-invariant");
}

bool micros_vm_handoff_syscall_vm_is_authorized(
    const struct micros_vm_handoff_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;

    return (
        state != NULL
        && registry != NULL
        && objects != NULL
        && context->process.slot == state->process.slot
        && context->process.generation == state->process.generation
        && context->current.slot == state->thread.slot
        && context->current.generation == state->thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->endpoint
        && process->privilege_profile == state->profile_id
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)state->profile_id,
            1
        ) == MICROS_ENDPOINT_OK
    );
}

bool micros_vm_handoff_syscall_vm_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    size_t index;

    if (
        bootstrap == NULL
        || state == NULL
        || ownership == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || bootstrap->transitions.starting_service_id
            != state->service_id
        || state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return false;
    }
    for (
        index = 0;
        index < bootstrap->transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap->transitions.entries[index];

        if (entry->service_id == state->service_id) {
            return entry->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING;
        }
    }
    return false;
}

bool micros_vm_handoff_syscall_post_handoff_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    return (
        bootstrap != NULL
        && state != NULL
        && ownership != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && state->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && ownership->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
}

enum reservation_check_result micros_vm_handoff_syscall_check_reservation(
    const struct micros_pm_control_state *pm,
    const struct micros_kernel_objects *objects,
    uint64_t token,
    const struct micros_process_handle *expected_process,
    bool require_empty,
    struct micros_process_handle *process_handle
)
{
    const struct micros_process *process;

    if (
        pm == NULL
        || objects == NULL
        || process_handle == NULL
        || token == 0
        || micros_pm_control_state_validate(pm)
            != MICROS_PM_CONTROL_OK
    ) {
        return RESERVATION_CHECK_INVARIANT;
    }
    if (
        pm->active_transaction != token
        || (
            expected_process != NULL
            && !micros_vm_handoff_syscall_process_handles_equal(
                pm->reserved_process,
                *expected_process
            )
        )
    ) {
        return RESERVATION_CHECK_STATE;
    }
    if (
        micros_process_resolve(
            objects,
            pm->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return RESERVATION_CHECK_INVARIANT;
    }
    if (require_empty && !micros_vm_handoff_syscall_process_is_empty(process)) {
        return RESERVATION_CHECK_INVARIANT;
    }
    *process_handle = pm->reserved_process;
    return RESERVATION_CHECK_OK;
}
