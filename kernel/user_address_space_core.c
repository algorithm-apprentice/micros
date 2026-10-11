#include "micros/user_address_space_core.h"

#include <stddef.h>
#include <stdint.h>

static bool process_is_valid(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

static bool page_address_is_valid(uint64_t address)
{
    return (
        address != 0
        && address % MICROS_SV39_PAGE_SIZE == 0
    );
}

static bool user_page_is_valid(uint64_t virtual_address)
{
    return (
        virtual_address >= MICROS_USER_VIRTUAL_BASE
        && virtual_address < MICROS_USER_VIRTUAL_END
        && virtual_address % MICROS_SV39_PAGE_SIZE == 0
        && micros_sv39_virtual_address_is_canonical(virtual_address)
    );
}

static int vm_permission_group(uint32_t permissions)
{
    const uint32_t text =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE
        | MICROS_SV39_PERMISSION_USER;
    const uint32_t read_only =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_USER;
    const uint32_t writable =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;

    if (permissions == text) {
        return 0;
    }
    if (permissions == read_only) {
        return 1;
    }
    return permissions == writable ? 2 : -1;
}

static bool scratch_authority_is_valid(
    const struct micros_user_address_space_vm_scratch_authority
        *authority
)
{
    size_t index;

    if (
        authority == NULL
        || !authority->active
        || authority->load_token == 0
        || !process_is_valid(authority->process)
        || authority->reserved1 != 0
        || !page_address_is_valid(
            authority->root_physical_address
        )
        || authority->virtual_address
            != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || !page_address_is_valid(authority->physical_address)
    ) {
        return false;
    }
    for (index = 0; index < sizeof(authority->reserved0); ++index) {
        if (authority->reserved0[index] != 0) {
            return false;
        }
    }
    return true;
}

enum micros_user_address_space_error
micros_user_address_space_leaf_owner_kind(
    enum micros_frame_ownership_phase phase,
    enum micros_frame_owner_kind *kind
)
{
    enum micros_frame_owner_kind candidate;

    if (kind == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    switch (phase) {
    case MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP:
        candidate = MICROS_FRAME_OWNER_PROCESS_USER;
        break;
    case MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF:
        candidate = MICROS_FRAME_OWNER_VM_WIRED;
        break;
    default:
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    *kind = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_validate_frame_owner(
    enum micros_frame_ownership_phase phase,
    enum micros_user_address_space_frame_role role,
    struct micros_process_handle process,
    struct micros_frame_owner owner
)
{
    enum micros_frame_owner_kind expected_kind;

    if (
        process.slot >= MICROS_PROCESS_CAPACITY
        || process.generation == 0
        || process.generation > MICROS_PROCESS_GENERATION_MAX
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (role == MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE) {
        if (
            phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
            && phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        }
        expected_kind = MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE;
    } else if (role == MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF) {
        enum micros_user_address_space_error error =
            micros_user_address_space_leaf_owner_kind(
                phase,
                &expected_kind
            );

        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    } else {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        owner.kind != expected_kind
        || owner.slot != process.slot
        || owner.generation != process.generation
        || owner.reserved != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_classify_managed_leaf(
    enum micros_frame_ownership_phase phase,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf,
    struct micros_frame_owner owner,
    const struct micros_user_address_space_vm_scratch_authority
        *scratch_authority,
    enum micros_user_address_space_leaf_class *classification
)
{
    enum micros_user_address_space_leaf_class candidate;
    const uint32_t scratch_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;

    if (
        leaf == NULL
        || classification == NULL
        || !process_is_valid(process)
        || !page_address_is_valid(root_physical_address)
        || !user_page_is_valid(virtual_address)
        || leaf->kind != MICROS_SV39_PTE_LEAF
        || (
            leaf->permissions & MICROS_SV39_PERMISSION_USER
        ) == 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        scratch_authority != NULL
        && scratch_authority->active
        && !scratch_authority_is_valid(scratch_authority)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (
        scratch_authority != NULL
        && scratch_authority->active
        && scratch_authority->process.slot == process.slot
        && scratch_authority->process.generation
            == process.generation
        && scratch_authority->root_physical_address
            == root_physical_address
        && scratch_authority->virtual_address == virtual_address
    ) {
        if (
            phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
            || leaf->physical_address
                != scratch_authority->physical_address
            || leaf->permissions != scratch_permissions
            || !leaf->accessed
            || !leaf->dirty
            || owner.kind != MICROS_FRAME_OWNER_VM_TRANSFERABLE
            || owner.slot != 0
            || owner.generation != 0
            || owner.reserved != 0
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
        candidate = MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH;
    } else {
        enum micros_user_address_space_error error =
            micros_user_address_space_validate_frame_owner(
                phase,
                MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
                process,
                owner
            );

        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        candidate = MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    }
    *classification = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_table_plan(
    struct micros_process_handle process,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_user_address_space_vm_mapping *mappings,
    size_t mapping_count,
    struct micros_user_address_space_vm_table_plan *plan
)
{
    struct micros_user_address_space_vm_table_plan candidate = {0};
    size_t image_page_count;
    size_t index;
    bool saw_read_only = false;
    bool saw_writable = false;
    int prior_group = 0;

    if (
        table_frames == NULL
        || mappings == NULL
        || plan == NULL
        || !process_is_valid(process)
        || mapping_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1
        || mapping_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    image_page_count = mapping_count - 1;
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        size_t prior_index;

        if (!page_address_is_valid(table_frames[index])) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
        }
        for (prior_index = 0; prior_index < index; ++prior_index) {
            if (table_frames[prior_index] == table_frames[index]) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        }
        candidate.table_frames[index] = table_frames[index];
    }
    if (
        micros_sv39_make_table_pte(
            table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE
            ],
            &candidate.root_pte
        ) != MICROS_SV39_OK
        || micros_sv39_make_table_pte(
            table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
            ],
            &candidate.image_middle_pte
        ) != MICROS_SV39_OK
        || micros_sv39_make_table_pte(
            table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
            ],
            &candidate.stack_middle_pte
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }

    for (index = 0; index < mapping_count; ++index) {
        const struct micros_user_address_space_vm_mapping *mapping =
            &mappings[index];
        struct micros_user_address_space_vm_leaf_plan *leaf =
            &candidate.leaves[index];
        uint16_t root_index;
        uint16_t middle_index;
        uint16_t leaf_index;
        size_t prior_index;
        int group = vm_permission_group(mapping->permissions);

        if (
            mapping->reserved != 0
            || !user_page_is_valid(mapping->virtual_address)
            || mapping->virtual_address
                == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
            || !page_address_is_valid(mapping->physical_address)
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
        }
        for (
            prior_index = 0;
            prior_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
            ++prior_index
        ) {
            if (
                mapping->physical_address
                    == table_frames[prior_index]
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        }
        for (prior_index = 0; prior_index < index; ++prior_index) {
            if (
                mappings[prior_index].physical_address
                    == mapping->physical_address
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        }
        if (
            micros_sv39_vpn_index(
                mapping->virtual_address,
                2,
                &root_index
            ) != MICROS_SV39_OK
            || micros_sv39_vpn_index(
                mapping->virtual_address,
                1,
                &middle_index
            ) != MICROS_SV39_OK
            || micros_sv39_vpn_index(
                mapping->virtual_address,
                0,
                &leaf_index
            ) != MICROS_SV39_OK
            || root_index != 1
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        }
        if (index < image_page_count) {
            if (
                mapping->virtual_address
                    != MICROS_APPLICATION_ENTRY
                        + index * MICROS_SV39_PAGE_SIZE
                || group < 0
                || (index == 0 && group != 0)
                || (
                    index != 0
                    && (
                        group < prior_group
                        || group > prior_group + 1
                    )
                )
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
            }
            if (index == 0) {
                candidate.root_index = root_index;
                candidate.image_middle_index = middle_index;
            } else if (
                middle_index != candidate.image_middle_index
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
            }
            saw_read_only = saw_read_only || group == 1;
            saw_writable = saw_writable || group == 2;
            prior_group = group;
            leaf->table_role =
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE;
        } else {
            if (
                mapping->virtual_address
                    != MICROS_APPLICATION_STACK_BASE
                || group != 2
                || prior_group != 2
                || !saw_read_only
                || !saw_writable
                || middle_index == candidate.image_middle_index
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
            }
            candidate.stack_middle_index = middle_index;
            leaf->table_role =
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK;
        }
        leaf->leaf_index = leaf_index;
        if (
            micros_sv39_make_leaf_pte(
                mapping->physical_address,
                mapping->permissions,
                &leaf->pte
            ) != MICROS_SV39_OK
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
    }
    candidate.process = process;
    candidate.entry_count = (uint32_t)mapping_count;
    *plan = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}
