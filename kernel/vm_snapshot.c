#include "kernel/vm_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/user_address_space.h"

struct mapping_context {
    struct micros_vm_boot_info *info;
    struct micros_process_handle process;
    size_t next_mapping;
};

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
static struct micros_vm_boot_info snapshot;

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool collect_mapping(
    void *opaque,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    struct mapping_context *context = opaque;
    struct micros_vm_mapping *mapping;

    if (
        context == NULL
        || context->next_mapping >= MICROS_VM_MAX_STATIC_MAPPINGS
        || permissions > UINT8_MAX
    ) {
        return false;
    }
    mapping = &context->info->mappings[context->next_mapping];
    *mapping = (struct micros_vm_mapping){
        .process_generation = context->process.generation,
        .process_slot = context->process.slot,
        .permissions = (uint8_t)permissions,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
    };
    ++context->next_mapping;
    return true;
}

static bool binding_matches_manifest(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *binding
)
{
    return (
        binding->manifest_index < manifest->header.entry_count
        && manifest->entries[binding->manifest_index].service_id
            == binding->service_id
        && manifest->entries[binding->manifest_index].process_slot
            == binding->process.slot
        && binding->root != 0
        && binding->endpoint != MICROS_ENDPOINT_NONE
        && binding->endpoint != MICROS_ENDPOINT_ANY
    );
}

enum micros_bootstrap_error micros_vm_snapshot_prepare(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count,
    struct micros_vm_snapshot_result *result
)
{
    const struct micros_frame_allocator *allocator;
    const struct micros_frame_ownership *ownership;
    struct micros_vm_snapshot_result candidate;
    struct micros_process_handle vm_process = {0};
    uint32_t vm_service_id = 0;
    uint32_t vm_endpoint = MICROS_ENDPOINT_NONE;
    size_t vm_binding_index = SIZE_MAX;
    size_t mapping_count = 0;
    size_t index;

    if (
        manifest == NULL
        || bindings == NULL
        || result == NULL
        || binding_count == 0
        || binding_count != manifest->header.entry_count
        || binding_count > MICROS_VM_MAX_STATIC_ADDRESS_SPACES
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    clear_bytes(&snapshot, sizeof(snapshot));
    for (index = 0; index < binding_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &bindings[index];
        const struct micros_bootstrap_manifest_entry *entry;

        if (!binding_matches_manifest(manifest, binding)) {
            clear_bytes(&snapshot, sizeof(snapshot));
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        entry = &manifest->entries[binding->manifest_index];
        if ((entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0) {
            if (vm_binding_index != SIZE_MAX) {
                clear_bytes(&snapshot, sizeof(snapshot));
                return MICROS_BOOTSTRAP_ERROR_ROLE;
            }
            vm_binding_index = index;
            vm_service_id = binding->service_id;
            vm_endpoint = binding->endpoint;
            vm_process = binding->process;
        }
    }
    if (vm_binding_index == SIZE_MAX) {
        clear_bytes(&snapshot, sizeof(snapshot));
        return MICROS_BOOTSTRAP_ERROR_ROLE;
    }
    if (
        micros_user_address_space_prepare_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        clear_bytes(&snapshot, sizeof(snapshot));
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (index = 0; index < binding_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &bindings[index];
        struct micros_vm_address_space *space =
            &snapshot.address_spaces[index];
        struct mapping_context context = {
            .info = &snapshot,
            .process = binding->process,
            .next_mapping = mapping_count,
        };
        uint64_t root;
        size_t process_mapping_count;

        if (
            micros_user_address_space_inventory(
                binding->process,
                collect_mapping,
                &context,
                &root,
                &process_mapping_count
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || root != binding->root
            || process_mapping_count == 0
            || process_mapping_count > UINT16_MAX
            || context.next_mapping
                != mapping_count + process_mapping_count
        ) {
            clear_bytes(&snapshot, sizeof(snapshot));
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        *space = (struct micros_vm_address_space){
            .service_id = binding->service_id,
            .endpoint = binding->endpoint,
            .process_generation = binding->process.generation,
            .process_slot = binding->process.slot,
            .mapping_count = (uint16_t)process_mapping_count,
            .mapping_index = (uint32_t)mapping_count,
            .root_physical_address = root,
        };
        mapping_count = context.next_mapping;
    }
    allocator = micros_bootstrap_frame_allocator();
    ownership = micros_frame_ownership_runtime_ledger();
    if (
        allocator == NULL
        || ownership == NULL
        || micros_vm_boot_build(
            &snapshot,
            binding_count,
            mapping_count,
            allocator,
            ownership,
            vm_service_id,
            vm_endpoint,
            vm_process,
            &candidate.summary
        ) != MICROS_VM_BOOT_OK
    ) {
        clear_bytes(&snapshot, sizeof(snapshot));
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    candidate.info = &snapshot;
    candidate.vm_binding_index = (uint16_t)vm_binding_index;
    copy_bytes(result, &candidate, sizeof(candidate));
    return MICROS_BOOTSTRAP_OK;
}
