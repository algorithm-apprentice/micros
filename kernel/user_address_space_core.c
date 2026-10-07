#include "micros/user_address_space_core.h"

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
