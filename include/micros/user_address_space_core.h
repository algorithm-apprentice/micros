#ifndef MICROS_USER_ADDRESS_SPACE_CORE_H
#define MICROS_USER_ADDRESS_SPACE_CORE_H

#include "micros/frame_ownership.h"
#include "micros/user_address_space.h"

enum micros_user_address_space_frame_role {
    MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE = 1,
    MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
};

enum micros_user_address_space_error
micros_user_address_space_leaf_owner_kind(
    enum micros_frame_ownership_phase phase,
    enum micros_frame_owner_kind *kind
);

enum micros_user_address_space_error
micros_user_address_space_validate_frame_owner(
    enum micros_frame_ownership_phase phase,
    enum micros_user_address_space_frame_role role,
    struct micros_process_handle process,
    struct micros_frame_owner owner
);

#endif
