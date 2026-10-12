#include "kernel/vm_snapshot_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/user_address_space.h"

struct mapping_validation_context {
    const struct micros_vm_boot_info *expected;
    struct micros_process_handle process;
    size_t next_mapping;
    size_t mapping_end;
};

static bool validate_mapping(
    void *opaque,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    struct mapping_validation_context *context = opaque;
    const struct micros_vm_mapping *mapping;

    if (
        context == NULL
        || context->next_mapping >= context->mapping_end
    ) {
        return false;
    }
    mapping = &context->expected->mappings[context->next_mapping];
    if (
        mapping->process_slot != context->process.slot
        || mapping->process_generation != context->process.generation
        || mapping->virtual_address != virtual_address
        || mapping->physical_address != physical_address
        || mapping->permissions != permissions
    ) {
        return false;
    }
    ++context->next_mapping;
    return true;
}
enum micros_bootstrap_error micros_vm_snapshot_validate_current(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count,
    const struct micros_vm_boot_info *expected
)
{
    const struct micros_frame_allocator *allocator;
    const struct micros_frame_ownership *ownership;
    struct micros_process_handle vm_process = {0};
    bool found_vm = false;
    size_t mapping_index = 0;
    size_t index;

    if (
        manifest == NULL
        || bindings == NULL
        || expected == NULL
        || binding_count == 0
        || binding_count != manifest->header.entry_count
        || binding_count != expected->header.address_space_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    for (index = 0; index < binding_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &bindings[index];
        const struct micros_bootstrap_manifest_entry *entry;
        const struct micros_vm_address_space *space =
            &expected->address_spaces[index];
        struct mapping_validation_context context;
        uint64_t root;
        size_t process_mapping_count;

        if (!micros_vm_snapshot_binding_matches_manifest(manifest, binding)) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        entry = &manifest->entries[binding->manifest_index];
        if (
            space->service_id != binding->service_id
            || space->endpoint != binding->endpoint
            || space->process_slot != binding->process.slot
            || space->process_generation
                != binding->process.generation
            || space->mapping_index != mapping_index
            || space->mapping_count == 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        if ((entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0) {
            if (found_vm) {
                return MICROS_BOOTSTRAP_ERROR_INVARIANT;
            }
            found_vm = true;
            vm_process = binding->process;
        }
        context = (struct mapping_validation_context){
            .expected = expected,
            .process = binding->process,
            .next_mapping = mapping_index,
            .mapping_end = mapping_index + space->mapping_count,
        };
        if (
            context.mapping_end > expected->header.mapping_count
            || micros_user_address_space_inventory(
                binding->process,
                validate_mapping,
                &context,
                &root,
                &process_mapping_count
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || root != space->root_physical_address
            || root != binding->root
            || process_mapping_count != space->mapping_count
            || context.next_mapping != context.mapping_end
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        mapping_index = context.mapping_end;
    }
    allocator = micros_bootstrap_frame_allocator();
    ownership = micros_frame_ownership_runtime_ledger();
    if (
        !found_vm
        || mapping_index != expected->header.mapping_count
        || allocator == NULL
        || ownership == NULL
        || micros_vm_boot_validate_authority(
            expected,
            allocator,
            ownership,
            vm_process
        ) != MICROS_VM_BOOT_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}
