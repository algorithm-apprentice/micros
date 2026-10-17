#include "micros/user_address_space_core.h"

#include <stddef.h>
#include <stdint.h>

static bool process_is_valid(struct micros_process_handle process) {
    return (process.slot < MICROS_PROCESS_CAPACITY && process.generation != 0 &&
            process.generation <= MICROS_PROCESS_GENERATION_MAX);
}

static bool page_address_is_valid(uint64_t address) {
    return (address != 0 && address % MICROS_SV39_PAGE_SIZE == 0);
}

static bool user_page_is_valid(uint64_t virtual_address) {
    return (virtual_address >= MICROS_USER_VIRTUAL_BASE &&
            virtual_address < MICROS_USER_VIRTUAL_END &&
            virtual_address % MICROS_SV39_PAGE_SIZE == 0 &&
            micros_sv39_virtual_address_is_canonical(virtual_address));
}

static bool
scratch_authority_is_valid(const struct micros_user_address_space_vm_scratch_authority *authority) {
    size_t index;

    if (authority == NULL || !authority->active || authority->load_token == 0 ||
        !process_is_valid(authority->process) || authority->reserved1 != 0 ||
        !page_address_is_valid(authority->root_physical_address) ||
        authority->virtual_address != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS ||
        !page_address_is_valid(authority->physical_address)) {
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
micros_user_address_space_leaf_owner_kind(enum micros_frame_ownership_phase phase,
                                          enum micros_frame_owner_kind *kind) {
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

enum micros_user_address_space_error micros_user_address_space_validate_frame_owner(
    enum micros_frame_ownership_phase phase, enum micros_user_address_space_frame_role role,
    struct micros_process_handle process, struct micros_frame_owner owner) {
    enum micros_frame_owner_kind expected_kind;

    if (process.slot >= MICROS_PROCESS_CAPACITY || process.generation == 0 ||
        process.generation > MICROS_PROCESS_GENERATION_MAX) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (role == MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE) {
        if (phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP &&
            phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        }
        expected_kind = MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE;
    } else if (role == MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF) {
        enum micros_user_address_space_error error =
            micros_user_address_space_leaf_owner_kind(phase, &expected_kind);

        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    } else {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (owner.kind != expected_kind || owner.slot != process.slot ||
        owner.generation != process.generation || owner.reserved != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_classify_managed_leaf(
    enum micros_frame_ownership_phase phase, struct micros_process_handle process,
    uint64_t root_physical_address, uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf, struct micros_frame_owner owner,
    const struct micros_user_address_space_vm_scratch_authority *scratch_authority,
    enum micros_user_address_space_leaf_class *classification) {
    enum micros_user_address_space_leaf_class candidate;
    const uint32_t scratch_permissions =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER;

    if (leaf == NULL || classification == NULL || !process_is_valid(process) ||
        !page_address_is_valid(root_physical_address) || !user_page_is_valid(virtual_address) ||
        leaf->kind != MICROS_SV39_PTE_LEAF ||
        (leaf->permissions & MICROS_SV39_PERMISSION_USER) == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (scratch_authority != NULL && scratch_authority->active &&
        !scratch_authority_is_valid(scratch_authority)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (scratch_authority != NULL && scratch_authority->active &&
        scratch_authority->process.slot == process.slot &&
        scratch_authority->process.generation == process.generation &&
        scratch_authority->root_physical_address == root_physical_address &&
        scratch_authority->virtual_address == virtual_address) {
        if (phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF ||
            leaf->physical_address != scratch_authority->physical_address ||
            leaf->permissions != scratch_permissions || !leaf->accessed || !leaf->dirty ||
            owner.kind != MICROS_FRAME_OWNER_VM_TRANSFERABLE || owner.slot != 0 ||
            owner.generation != 0 || owner.reserved != 0) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
        candidate = MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH;
    } else {
        enum micros_user_address_space_error error = micros_user_address_space_validate_frame_owner(
            phase, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process, owner);

        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        candidate = MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    }
    *classification = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}
