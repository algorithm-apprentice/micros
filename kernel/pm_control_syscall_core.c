#include "kernel/pm_control_syscall_core.h"

#include <stddef.h>
#include <stdint.h>

static void copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool micros_pm_control_syscall_phase_is_ready(
    enum micros_bootstrap_phase bootstrap_phase,
    enum micros_vm_handoff_phase vm_handoff_phase,
    enum micros_frame_ownership_phase ownership_phase
)
{
    return (
        bootstrap_phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && vm_handoff_phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && ownership_phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
}

bool micros_pm_control_syscall_authority_matches(
    const struct micros_pm_control_state *state,
    uint32_t planned_pm_service_id,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_syscall_context *context,
    const struct micros_process *process,
    const struct micros_endpoint_record *endpoint,
    const struct micros_privilege_profile *profile
)
{
    return (
        state != NULL
        && binding != NULL
        && entry != NULL
        && context != NULL
        && process != NULL
        && endpoint != NULL
        && profile != NULL
        && planned_pm_service_id != 0
        && planned_pm_service_id == binding->service_id
        && entry->service_id == binding->service_id
        && entry->process_slot == binding->process.slot
        && entry->role_flags == MICROS_BOOTSTRAP_ROLE_PM
        && entry->profile_id == MICROS_PRIVILEGE_PROFILE_PM
        && state->service_id == binding->service_id
        && state->endpoint == binding->endpoint
        && state->profile_id == entry->profile_id
        && process_handles_equal(state->process, binding->process)
        && thread_handles_equal(state->thread, binding->thread)
        && process_handles_equal(context->process, binding->process)
        && thread_handles_equal(context->current, binding->thread)
        && process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
        && process->generation == binding->process.generation
        && process->primary_endpoint == binding->endpoint
        && process->privilege_profile == entry->profile_id
        && endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
        && endpoint->value == binding->endpoint
        && process_handles_equal(endpoint->owner, binding->process)
        && profile->id == entry->profile_id
        && profile->kernel_operations
            == MICROS_KERNEL_OPERATION_PM_CONTROL
    );
}

enum micros_pm_control_output_error
micros_pm_control_output_prepare(
    uint64_t user_address,
    micros_pm_control_output_translate translate,
    void *translate_context,
    struct micros_pm_control_output_plan *plan
)
{
    struct micros_pm_control_output_plan candidate = {0};
    uint64_t current = user_address;
    size_t remaining = sizeof(struct micros_pm_reservation_result);

    if (translate == NULL || plan == NULL) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    while (remaining != 0) {
        uintptr_t physical_address = 0;
        size_t contiguous = 0;
        size_t chunk;
        enum micros_pm_control_output_error error;

        if (
            candidate.chunk_count
                >= MICROS_PM_CONTROL_OUTPUT_CHUNK_CAPACITY
        ) {
            return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
        }
        error = translate(
            translate_context,
            current,
            remaining,
            &physical_address,
            &contiguous
        );
        if (error == MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT) {
            return error;
        }
        if (
            error != MICROS_PM_CONTROL_OUTPUT_OK
            || physical_address == 0
            || contiguous == 0
        ) {
            return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
        }
        chunk = contiguous < remaining ? contiguous : remaining;
        if (
            physical_address > UINTPTR_MAX - (chunk - 1)
            || current > UINT64_MAX - chunk
        ) {
            return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
        }
        candidate.chunks[candidate.chunk_count].physical_address =
            physical_address;
        candidate.chunks[candidate.chunk_count].size = chunk;
        ++candidate.chunk_count;
        current += chunk;
        remaining -= chunk;
    }
    *plan = candidate;
    return MICROS_PM_CONTROL_OUTPUT_OK;
}

void micros_pm_control_output_commit_prevalidated(
    const struct micros_pm_control_output_plan *plan,
    const struct micros_pm_reservation_result *result
)
{
    const unsigned char *input = (const unsigned char *)result;
    size_t input_offset = 0;
    size_t chunk_index;

    for (chunk_index = 0; chunk_index < plan->chunk_count; ++chunk_index) {
        void *destination =
            (void *)plan->chunks[chunk_index].physical_address;

        copy_bytes(
            destination,
            &input[input_offset],
            plan->chunks[chunk_index].size
        );
        input_offset += plan->chunks[chunk_index].size;
    }
}
