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

static bool endpoint_owner_decode(
    micros_endpoint_t endpoint,
    struct micros_process_handle *process
)
{
    struct micros_process_handle candidate;

    if (
        process == NULL
        || endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return false;
    }
    candidate.slot =
        (uint16_t)(endpoint & ((1U << MICROS_ENDPOINT_SLOT_BITS) - 1));
    candidate.generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        candidate.generation == 0
        || candidate.generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return false;
    }
    *process = candidate;
    return true;
}

static bool endpoint_matches_process(
    micros_endpoint_t endpoint,
    struct micros_process_handle process
)
{
    struct micros_process_handle owner;

    return (
        endpoint_owner_decode(endpoint, &owner)
        && process_handles_equal(owner, process)
    );
}

static const struct micros_endpoint_record *authority_endpoint(
    const struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
)
{
    struct micros_process_handle owner;

    if (
        registry == NULL
        || !endpoint_owner_decode(endpoint, &owner)
        || owner.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return NULL;
    }
    return &registry->endpoints[owner.slot];
}

static const struct micros_privilege_profile *authority_profile(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id
)
{
    if (
        registry == NULL
        || profile_id == 0
        || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
    ) {
        return NULL;
    }
    return &registry->profiles[profile_id];
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

enum micros_pm_control_authority_result
micros_pm_control_syscall_caller_classify(
    const struct micros_pm_control_state *state,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread
)
{
    if (
        state == NULL
        || context == NULL
        || caller_process == NULL
        || caller_thread == NULL
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        caller_process->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || caller_process->generation
            != context->process.generation
        || caller_thread->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || caller_thread->generation
            != context->current.generation
        || !process_handles_equal(
            caller_thread->owner,
            context->process
        )
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        !process_handles_equal(context->process, state->process)
        || !thread_handles_equal(context->current, state->thread)
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED;
    }
    return MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED;
}

enum micros_pm_control_authority_result
micros_pm_control_syscall_authority_classify(
    const struct micros_pm_control_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_pm_service_id,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread,
    const struct micros_endpoint_record *endpoint,
    const struct micros_privilege_profile *profile
)
{
    enum micros_pm_control_authority_result caller_result;

    caller_result = micros_pm_control_syscall_caller_classify(
        state,
        context,
        caller_process,
        caller_thread
    );
    if (caller_result != MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED) {
        return caller_result;
    }
    if (
        bootstrap_entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        binding == NULL
        || entry == NULL
        || endpoint == NULL
        || profile == NULL
        || bootstrap_entry_count == 0
        || binding->manifest_index >= bootstrap_entry_count
        || planned_pm_service_id == 0
        || planned_pm_service_id != binding->service_id
        || entry->service_id != binding->service_id
        || entry->process_slot != binding->process.slot
        || entry->role_flags != MICROS_BOOTSTRAP_ROLE_PM
        || entry->profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || state->service_id != binding->service_id
        || state->endpoint != binding->endpoint
        || state->profile_id != entry->profile_id
        || !process_handles_equal(state->process, binding->process)
        || !thread_handles_equal(state->thread, binding->thread)
        || !endpoint_matches_process(
            binding->endpoint,
            binding->process
        )
        || caller_process->primary_endpoint != binding->endpoint
        || caller_process->privilege_profile != entry->profile_id
        || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || endpoint->value != binding->endpoint
        || !process_handles_equal(
            endpoint->owner,
            binding->process
        )
        || profile->id != entry->profile_id
        || profile->kernel_operations
            != MICROS_KERNEL_OPERATION_PM_CONTROL
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED;
    }
    return MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED;
}

enum micros_pm_control_authority_result
micros_pm_control_syscall_authority_resolve(
    const struct micros_pm_control_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    const struct micros_privilege_profile *profile;
    const struct micros_process *caller_process;
    const struct micros_thread *caller_thread;
    const struct micros_endpoint_record *endpoint;
    enum micros_pm_control_authority_result caller_result;

    if (objects == NULL || context == NULL) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        micros_process_resolve(
            objects,
            context->process,
            &caller_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            context->current,
            &caller_thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    caller_result = micros_pm_control_syscall_caller_classify(
        state,
        context,
        caller_process,
        caller_thread
    );
    if (caller_result != MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED) {
        return caller_result;
    }
    if (
        bootstrap == NULL
        || registry == NULL
        || bootstrap->entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        bootstrap->entry_count == 0
        || bootstrap->plan.pm_service_id == 0
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED;
    }
    binding = micros_bootstrap_control_find_binding_bounded(
        bootstrap,
        bootstrap->plan.pm_service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED;
    }
    entry = &bootstrap->manifest.entries[binding->manifest_index];
    endpoint = authority_endpoint(registry, binding->endpoint);
    profile = authority_profile(registry, entry->profile_id);
    return micros_pm_control_syscall_authority_classify(
        state,
        bootstrap->entry_count,
        bootstrap->plan.pm_service_id,
        binding,
        entry,
        context,
        caller_process,
        caller_thread,
        endpoint,
        profile
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
