#ifndef MICROS_USER_ADDRESS_SPACE_CORE_H
#define MICROS_USER_ADDRESS_SPACE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/frame_ownership.h"
#include "micros/spawn.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"

enum micros_user_address_space_frame_role {
    MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE = 1,
    MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
};

enum micros_user_address_space_leaf_class {
    MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR = 1,
    MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH,
};

enum micros_user_address_space_vm_table_role {
    MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT = 0,
    MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE,
    MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE,
    MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK,
};

struct micros_user_address_space_vm_scratch_authority {
    bool active;
    uint8_t reserved0[7];
    uint64_t load_token;
    struct micros_process_handle process;
    uint32_t reserved1;
    uint64_t root_physical_address;
    uint64_t virtual_address;
    uint64_t physical_address;
};

struct micros_user_address_space_vm_mapping {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    uint32_t reserved;
};

struct micros_user_address_space_vm_leaf_plan {
    uint16_t table_role;
    uint16_t leaf_index;
    uint32_t reserved;
    uint64_t pte;
};

struct micros_user_address_space_vm_table_plan {
    struct micros_process_handle process;
    uint32_t entry_count;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint16_t root_index;
    uint16_t image_middle_index;
    uint16_t stack_middle_index;
    uint16_t reserved;
    uint64_t root_pte;
    uint64_t image_middle_pte;
    uint64_t stack_middle_pte;
    struct micros_user_address_space_vm_leaf_plan
        leaves[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
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
);

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_table_plan(
    struct micros_process_handle process,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_user_address_space_vm_mapping *mappings,
    size_t mapping_count,
    struct micros_user_address_space_vm_table_plan *plan
);

#endif
