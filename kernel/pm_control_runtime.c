#include "kernel/pm_control_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/frame_ownership_runtime.h"

static struct micros_pm_control_state control_state;

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
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

static bool owner_is_process_bound(struct micros_frame_owner owner)
{
    return (
        owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
        || owner.kind == MICROS_FRAME_OWNER_VM_WIRED
    );
}

static bool process_frame_owners_are_live(
    const struct micros_frame_ownership *ownership,
    const struct micros_kernel_objects *objects
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ownership->owners[frame_index];
        const struct micros_process *process;

        if (!owner_is_process_bound(owner)) {
            continue;
        }
        if (owner.slot >= MICROS_PROCESS_CAPACITY) {
            return false;
        }
        process = &objects->processes[owner.slot];
        if (
            process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != owner.generation
        ) {
            return false;
        }
    }
    return true;
}

enum micros_pm_control_error micros_pm_control_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry
)
{
    if (
        binding == NULL
        || entry == NULL
        || binding->service_id != entry->service_id
        || binding->process.slot != entry->process_slot
        || entry->role_flags != MICROS_BOOTSTRAP_ROLE_PM
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    return micros_pm_control_state_prepare(
        &control_state,
        binding->service_id,
        binding->endpoint,
        entry->profile_id,
        binding->process,
        binding->thread
    );
}

enum micros_pm_control_error micros_pm_control_runtime_reset(void)
{
    if (control_state.service_id == 0) {
        clear_bytes(&control_state, sizeof(control_state));
        return MICROS_PM_CONTROL_OK;
    }
    if (
        micros_pm_control_state_validate(&control_state)
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (control_state.active_transaction != 0) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    clear_bytes(&control_state, sizeof(control_state));
    return MICROS_PM_CONTROL_OK;
}

const struct micros_pm_control_state *micros_pm_control_runtime_state(
    void
)
{
    return control_state.service_id == 0 ? NULL : &control_state;
}

struct micros_pm_control_state *
micros_pm_control_runtime_authoritative_state(void)
{
    return control_state.service_id == 0 ? NULL : &control_state;
}

enum micros_pm_control_error micros_pm_control_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    const struct micros_privilege_profile *profile;
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    uint64_t frame_count = 0;

    if (
        bootstrap == NULL
        || registry == NULL
        || objects == NULL
        || ownership == NULL
        || micros_frame_ownership_validate(ownership)
            != MICROS_FRAME_OWNERSHIP_OK
        || !process_frame_owners_are_live(ownership, objects)
        || micros_pm_control_state_validate_objects(
            &control_state,
            objects
        ) != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        control_state.service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    entry = &bootstrap->manifest.entries[binding->manifest_index];
    if (
        bootstrap->plan.pm_service_id != control_state.service_id
        || entry->role_flags != MICROS_BOOTSTRAP_ROLE_PM
        || entry->profile_id != control_state.profile_id
        || binding->endpoint != control_state.endpoint
        || !process_handles_equal(
            binding->process,
            control_state.process
        )
        || !thread_handles_equal(
            binding->thread,
            control_state.thread
        )
        || micros_process_resolve(
            objects,
            control_state.process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || process->address_space_root != binding->root
        || process->primary_endpoint != control_state.endpoint
        || process->privilege_profile != control_state.profile_id
        || micros_thread_resolve(
            objects,
            control_state.thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !process_handles_equal(
            thread->owner,
            control_state.process
        )
        || !thread->context_attached
        || micros_endpoint_resolve_active(
            registry,
            objects,
            control_state.endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || !process_handles_equal(
            endpoint->owner,
            control_state.process
        )
        || micros_privilege_profile_resolve(
            registry,
            (uint8_t)control_state.profile_id,
            &profile
        ) != MICROS_ENDPOINT_OK
        || profile->kernel_operations
            != MICROS_KERNEL_OPERATION_PM_CONTROL
        || micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)control_state.profile_id,
            2
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (
        control_state.active_transaction != 0
        && (
            micros_frame_ownership_count_process(
                ownership,
                control_state.reserved_process,
                &frame_count
            ) != MICROS_FRAME_OWNERSHIP_OK
            || frame_count != 0
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}
