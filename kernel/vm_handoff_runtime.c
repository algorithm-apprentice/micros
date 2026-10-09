#include "kernel/vm_handoff_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "micros/frame_ownership_runtime.h"

static struct micros_vm_handoff_state handoff_state;

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

enum micros_vm_handoff_error micros_vm_handoff_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_bootstrap_image *image,
    const struct micros_vm_snapshot_result *snapshot
)
{
    if (
        binding == NULL
        || entry == NULL
        || image == NULL
        || snapshot == NULL
        || snapshot->info == NULL
        || binding->service_id != entry->service_id
        || binding->process.slot != entry->process_slot
        || (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) == 0
        || image->image_id != entry->image_id
        || image->vm_boot_info_address == 0
        || image->vm_boot_info_size != MICROS_VM_BOOT_INFO_SIZE
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    return micros_vm_handoff_state_prepare(
        &handoff_state,
        binding->service_id,
        binding->endpoint,
        image->image_id,
        entry->profile_id,
        binding->process,
        binding->thread,
        binding->root,
        image->vm_boot_info_address,
        image->vm_boot_info_size,
        &snapshot->summary
    );
}

enum micros_vm_handoff_error micros_vm_handoff_runtime_reset(void)
{
    if (
        handoff_state.phase
            == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
    ) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    clear_bytes(&handoff_state, sizeof(handoff_state));
    return MICROS_VM_HANDOFF_OK;
}

const struct micros_vm_handoff_state *
micros_vm_handoff_runtime_state(void)
{
    return handoff_state.phase == MICROS_VM_HANDOFF_PHASE_UNINITIALIZED
        ? NULL
        : &handoff_state;
}

struct micros_vm_handoff_state *
micros_vm_handoff_runtime_authoritative_state(void)
{
    return handoff_state.phase == MICROS_VM_HANDOFF_PHASE_UNINITIALIZED
        ? NULL
        : &handoff_state;
}

enum micros_vm_handoff_error micros_vm_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    enum micros_frame_ownership_phase expected_ownership_phase;

    if (
        bootstrap == NULL
        || registry == NULL
        || objects == NULL
        || ownership == NULL
        || micros_vm_handoff_state_validate(&handoff_state)
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        handoff_state.service_id
    );
    if (binding == NULL) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    entry = &bootstrap->manifest.entries[binding->manifest_index];
    expected_ownership_phase = (
        handoff_state.phase == MICROS_VM_HANDOFF_PHASE_PREPARED
    )
        ? MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        : MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    if (
        bootstrap->plan.vm_service_id != handoff_state.service_id
        || (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) == 0
        || entry->image_id != handoff_state.image_id
        || entry->profile_id != handoff_state.profile_id
        || binding->endpoint != handoff_state.endpoint
        || binding->root != handoff_state.root_physical_address
        || !process_handles_equal(
            binding->process,
            handoff_state.process
        )
        || !thread_handles_equal(
            binding->thread,
            handoff_state.thread
        )
        || ownership->phase != expected_ownership_phase
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_process_resolve(
            objects,
            handoff_state.process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || process->address_space_root
            != handoff_state.root_physical_address
        || process->primary_endpoint != handoff_state.endpoint
        || process->privilege_profile != handoff_state.profile_id
        || micros_thread_resolve(
            objects,
            handoff_state.thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !process_handles_equal(
            thread->owner,
            handoff_state.process
        )
        || !thread->context_attached
        || micros_endpoint_resolve_active(
            registry,
            objects,
            handoff_state.endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || !process_handles_equal(
            endpoint->owner,
            handoff_state.process
        )
        || micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)handoff_state.profile_id,
            1
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    return MICROS_VM_HANDOFF_OK;
}

bool micros_vm_handoff_runtime_role_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    struct micros_process_handle process,
    micros_endpoint_t endpoint
)
{
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();

    return (
        micros_vm_handoff_state_validate(&handoff_state)
            == MICROS_VM_HANDOFF_OK
        && handoff_state.phase
            == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && ownership != NULL
        && ownership->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        && handoff_state.service_id == service_id
        && handoff_state.endpoint == endpoint
        && process_handles_equal(handoff_state.process, process)
        && micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_VM_HANDOFF_OK
    );
}
