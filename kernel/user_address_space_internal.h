#ifndef MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H
#define MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/vm_handoff_core.h"
#include "micros/user_address_space.h"
#include "micros/user_address_space_core.h"

struct micros_user_address_space_tty_uart_plan {
    bool active;
    uint16_t leaf_index;
    uint8_t reserved[5];
    struct micros_process_handle process;
    uint64_t root_physical_address;
    uint64_t leaf_table_physical_address;
    uint64_t leaf_pte;
};

struct micros_user_address_space_vm_scratch_plan {
    bool active;
    uint8_t reserved0[5];
    uint16_t leaf_index;
    struct micros_process_handle process;
    uint32_t reserved1;
    uint64_t root_physical_address;
    uint64_t leaf_table_physical_address;
    uint64_t physical_address;
    uint64_t leaf_pte;
};

enum micros_user_address_space_vm_freeze_plan_phase {
    MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_UNUSED = 0,
    MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED,
    MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED,
};

struct micros_user_address_space_vm_freeze_plan {
    uint32_t phase;
    uint32_t reserved;
    struct micros_user_address_space_vm_table_plan tables;
};

struct micros_user_address_space_vm_teardown_plan {
    bool active;
    uint8_t reserved0[3];
    uint32_t entry_count;
    struct micros_process_handle process;
    uint32_t reserved1;
    uint64_t root_physical_address;
    uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t
        data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
};

enum micros_user_address_space_error
micros_user_address_space_prepare_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    struct micros_user_address_space_tty_uart_plan *plan
);

void micros_user_address_space_commit_tty_uart_mapping(
    struct micros_user_address_space_tty_uart_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
);

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
);

void micros_user_address_space_commit_vm_scratch_mapping(
    struct micros_user_address_space_vm_scratch_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_removal(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
);

void micros_user_address_space_commit_vm_scratch_removal(
    struct micros_user_address_space_vm_scratch_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_freeze(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *plan
);

void micros_user_address_space_construct_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
);

void micros_user_address_space_install_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_teardown(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_teardown_plan *plan
);

void micros_user_address_space_commit_vm_teardown_prevalidated(
    struct micros_user_address_space_vm_teardown_plan *plan
);

#endif
