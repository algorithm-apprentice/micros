#ifndef MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H
#define MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/vm_handoff_core.h"
#include "micros/frame_ownership.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_objects.h"
#include "micros/user_address_space.h"
#include "micros/user_address_space_core.h"

enum {
    MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX = 1,
};

struct micros_user_address_space_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

struct micros_user_address_space_page_indices {
    uint16_t root;
    uint16_t middle;
    uint16_t leaf;
};

struct micros_user_address_space_runtime_view {
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *process;
    struct micros_process_handle process_handle;
    uint64_t process_root;
    enum micros_frame_ownership_phase ownership_phase;
};

struct micros_user_address_space_scratch_lease {
    bool active;
    uint64_t frame_count;
    size_t word_count;
    uint64_t *reachable_bitmap;
    uint64_t *release_bitmap;
};

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

uintptr_t micros_user_address_space_runtime_lock(void);

void micros_user_address_space_runtime_unlock(
    uintptr_t saved_status
);

enum micros_user_address_space_error
micros_user_address_space_runtime_require_bootstrap(
    struct micros_user_address_space_runtime_view *view
);

enum micros_user_address_space_error
micros_user_address_space_runtime_require_read(
    struct micros_user_address_space_runtime_view *view
);

enum micros_user_address_space_error
micros_user_address_space_runtime_resolve_process(
    struct micros_user_address_space_runtime_view *view,
    struct micros_process_handle process,
    bool require_root,
    const struct micros_process **resolved
);

enum micros_user_address_space_error
micros_user_address_space_map_object_error(
    enum micros_kernel_object_error error
);

enum micros_user_address_space_error
micros_user_address_space_map_ownership_error(
    enum micros_frame_ownership_error error
);

enum micros_user_address_space_error
micros_user_address_space_acquire_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
);

void micros_user_address_space_release_scratch(
    struct micros_user_address_space_scratch_lease *lease
);

bool micros_user_address_space_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

uint64_t micros_user_address_space_active_root(void);

uint64_t micros_user_address_space_expected_satp(uint64_t root);

_Noreturn void micros_user_address_space_panic_invariant(
    const char *reason
);

struct micros_user_address_space_page_table *
micros_user_address_space_table_at(
    uint64_t physical_address
);

void micros_user_address_space_clear_page(
    uint64_t physical_address
);

bool micros_user_address_space_user_page_is_valid(
    uint64_t virtual_address
);

bool micros_user_address_space_user_byte_address_is_valid(
    uint64_t virtual_address
);

bool micros_user_address_space_permissions_are_valid(
    uint32_t permissions
);

bool micros_user_address_space_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
);

bool micros_user_address_space_physical_address(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    uint64_t *physical_address
);

enum micros_user_address_space_error
micros_user_address_space_decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
);

enum micros_user_address_space_error
micros_user_address_space_read_page_indices(
    uint64_t virtual_address,
    struct micros_user_address_space_page_indices *indices
);

void micros_user_address_space_clear_bitmap(
    uint64_t *bitmap,
    size_t word_count
);

bool micros_user_address_space_bitmap_contains(
    const uint64_t *bitmap,
    size_t word_count,
    uint64_t frame_index
);

bool micros_user_address_space_bitmap_add(
    uint64_t *bitmap,
    size_t word_count,
    uint64_t frame_index
);

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
