#include "kernel/memory/user_address_space_internal.h"

#include "kernel/tty/tty_handoff_core.h"

static struct micros_user_address_space_page_table *table_at(
    uint64_t physical_address
)
{
    return micros_user_address_space_table_at(physical_address);
}

static enum micros_user_address_space_error decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    return micros_user_address_space_decode_pte(pte, decoded);
}

static enum micros_user_address_space_error
require_read_authority(
    struct micros_user_address_space_runtime_view *view,
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report
)
{
    enum micros_frame_owner_kind leaf_owner_kind;
    enum micros_user_address_space_error error =
        micros_user_address_space_runtime_require_read(view);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_leaf_owner_kind(
        view->ownership_phase,
        &leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *ledger = view->ledger;
    *objects = view->objects;
    *kernel_report = view->kernel_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error resolve_process_with_root(
    struct micros_user_address_space_runtime_view *view,
    struct micros_process_handle process,
    const struct micros_process **resolved
)
{
    return micros_user_address_space_runtime_resolve_process(
        view,
        process,
        true,
        resolved
    );
}

static bool user_virtual_address_is_valid(uint64_t virtual_address)
{
    return micros_user_address_space_user_page_is_valid(
        virtual_address
    );
}

static bool user_virtual_byte_address_is_valid(
    uint64_t virtual_address
)
{
    return micros_user_address_space_user_byte_address_is_valid(
        virtual_address
    );
}

static enum micros_user_address_space_error acquire_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
)
{
    return micros_user_address_space_acquire_scratch(view, lease);
}

static void release_scratch(
    struct micros_user_address_space_scratch_lease *lease
)
{
    micros_user_address_space_release_scratch(lease);
}

struct user_mapping_walk {
    struct micros_process_handle process_handle;
    const struct micros_process *process;
    micros_user_mapping_visitor visitor;
    void *visitor_context;
    struct micros_tty_device_authority device_authority;
    struct micros_user_address_space_vm_scratch_authority
        scratch_authority;
    size_t count;
    bool device_authority_active;
    bool scratch_authority_active;
};

static enum micros_user_address_space_error visit_leaf_table(
    struct user_mapping_walk *walk,
    size_t middle_index,
    uint64_t physical_address
)
{
    const struct micros_user_address_space_page_table *leaf =
        table_at(physical_address);
    size_t leaf_index;

    for (
        leaf_index = 0;
        leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++leaf_index
    ) {
        struct micros_sv39_decoded_pte leaf_entry;
        enum micros_user_address_space_error error;
        uint64_t virtual_address;
        bool is_device;

        error = decode_pte(leaf->entries[leaf_index], &leaf_entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (leaf_entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (
            leaf_entry.kind != MICROS_SV39_PTE_LEAF
            || (
                leaf_entry.permissions
                & MICROS_SV39_PERMISSION_USER
            ) == 0
            || walk->count == SIZE_MAX
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        virtual_address =
            MICROS_USER_VIRTUAL_BASE
            + (
                (
                    (uint64_t)middle_index
                    * MICROS_SV39_TABLE_ENTRY_COUNT
                ) + leaf_index
            ) * MICROS_SV39_PAGE_SIZE;
        error = micros_user_address_space_classify_tty_device_leaf(
            walk->device_authority_active,
            &walk->device_authority,
            walk->process_handle,
            walk->process->address_space_root,
            virtual_address,
            &leaf_entry,
            &is_device
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (is_device) {
            continue;
        }
        if (
            micros_user_address_space_leaf_matches_vm_scratch_authority(
                walk->scratch_authority_active,
                &walk->scratch_authority,
                walk->process_handle,
                walk->process->address_space_root,
                virtual_address,
                &leaf_entry
            )
        ) {
            continue;
        }
        if (
            !walk->visitor(
                walk->visitor_context,
                virtual_address,
                leaf_entry.physical_address,
                leaf_entry.permissions
                    & ~MICROS_SV39_PERMISSION_USER
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        }
        ++walk->count;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error visit_middle_table(
    struct user_mapping_walk *walk,
    uint64_t physical_address
)
{
    const struct micros_user_address_space_page_table *middle =
        table_at(physical_address);
    size_t middle_index;

    for (
        middle_index = 0;
        middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++middle_index
    ) {
        struct micros_sv39_decoded_pte middle_entry;
        enum micros_user_address_space_error error;

        error = decode_pte(
            middle->entries[middle_index],
            &middle_entry
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        error = visit_leaf_table(
            walk,
            middle_index,
            middle_entry.physical_address
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_visit_mappings_locked(
    struct micros_process_handle process_handle,
    const struct micros_process *process,
    micros_user_mapping_visitor visitor,
    void *context,
    size_t *mapping_count
)
{
    const struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct user_mapping_walk walk;
    enum micros_user_address_space_error error;

    walk.process_handle = process_handle;
    walk.process = process;
    walk.visitor = visitor;
    walk.visitor_context = context;
    walk.count = 0;
    error = micros_user_address_space_load_tty_device_authority(
        &walk.device_authority_active,
        &walk.device_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_load_vm_scratch_authority(
        &walk.scratch_authority_active,
        &walk.scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }

    error = decode_pte(
        root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        *mapping_count = 0;
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    error = visit_middle_table(&walk, root_entry.physical_address);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *mapping_count = walk.count;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_lookup_leaf_locked(
    const struct micros_process *process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf_entry,
    uint64_t **leaf_pte,
    uint64_t *middle_physical_address,
    uint64_t *leaf_physical_address
)
{
    struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;

    if (
        micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_entry->kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (leaf_entry->kind != MICROS_SV39_PTE_LEAF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (leaf_pte != NULL) {
        *leaf_pte = &leaf->entries[leaf_index];
    }
    if (middle_physical_address != NULL) {
        *middle_physical_address = root_entry.physical_address;
    }
    if (leaf_physical_address != NULL) {
        *leaf_physical_address = middle_entry.physical_address;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_locate_leaf_slot_locked(
    const struct micros_process *process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf_entry,
    uint64_t **leaf_pte,
    uint64_t *leaf_table_physical_address
)
{
    struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;

    if (
        leaf_entry == NULL
        || leaf_pte == NULL
        || leaf_table_physical_address == NULL
        || micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *leaf_pte = &leaf->entries[leaf_index];
    *leaf_table_physical_address = middle_entry.physical_address;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_lookup(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL || permissions == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = micros_user_address_space_lookup_leaf_locked(
        resolved,
        virtual_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        error = micros_user_address_space_reject_exceptional_leaf(
            process,
            resolved->address_space_root,
            virtual_address,
            &leaf_entry
        );
        if (error == MICROS_USER_ADDRESS_SPACE_OK) {
            *physical_address = leaf_entry.physical_address;
            *permissions = leaf_entry.permissions
                & ~MICROS_SV39_PERMISSION_USER;
        }
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uint64_t offset;
    uintptr_t saved_status;

    if (
        physical_address == NULL
        || permissions == NULL
        || contiguous_bytes == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_byte_address_is_valid(user_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = micros_user_address_space_lookup_leaf_locked(
        resolved,
        user_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        error = micros_user_address_space_reject_exceptional_leaf(
            process,
            resolved->address_space_root,
            user_address,
            &leaf_entry
        );
        if (error == MICROS_USER_ADDRESS_SPACE_OK) {
            offset = user_address % MICROS_SV39_PAGE_SIZE;
            *physical_address = leaf_entry.physical_address + offset;
            *permissions =
                leaf_entry.permissions
                & ~MICROS_SV39_PERMISSION_USER;
            *contiguous_bytes =
                (size_t)(MICROS_SV39_PAGE_SIZE - offset);
        }
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_inventory(
    struct micros_process_handle process,
    micros_user_mapping_visitor visitor,
    void *context,
    uint64_t *root_physical_address,
    size_t *mapping_count
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    size_t observed_mapping_count;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (
        visitor == NULL
        || root_physical_address == NULL
        || mapping_count == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = micros_user_address_space_visit_mappings_locked(
        process,
        resolved,
        visitor,
        context,
        &observed_mapping_count
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        *root_physical_address = resolved->address_space_root;
        *mapping_count = observed_mapping_count;
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}
