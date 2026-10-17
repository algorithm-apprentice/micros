#ifndef MICROS_KERNEL_USER_ADDRESS_SPACE_TEST_INTERNAL_H
#define MICROS_KERNEL_USER_ADDRESS_SPACE_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_objects.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

enum {
    MICROS_USER_ADDRESS_SPACE_TEST_LOAD_PAGE_FAULT = 13,
    MICROS_USER_ADDRESS_SPACE_TEST_TREE_FRAME_CAPACITY = 3,
    MICROS_USER_ADDRESS_SPACE_TEST_USER_FRAME_CAPACITY = 4,
    MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX = 1,
};

#define MICROS_USER_ADDRESS_SPACE_TEST_USER_VIRTUAL_ADDRESS \
    MICROS_USER_VIRTUAL_BASE
#define MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE)
#define MICROS_USER_ADDRESS_SPACE_TEST_ALIAS_VIRTUAL_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + (2 * MICROS_SV39_PAGE_SIZE))
#define MICROS_USER_ADDRESS_SPACE_TEST_GUARD_VIRTUAL_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + (3 * MICROS_SV39_PAGE_SIZE))
#define MICROS_USER_ADDRESS_SPACE_TEST_ACTIVE_VIRTUAL_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00400000))

struct micros_user_address_space_test_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

enum micros_user_address_space_test_sum_fault_state {
    MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_IDLE,
    MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_ARMED,
    MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_HANDLED,
};

struct micros_user_address_space_test_context {
    struct micros_frame_ownership ownership_snapshot;
    struct micros_frame_allocator allocator_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_user_address_space_test_page_table tree_snapshots
        [MICROS_USER_ADDRESS_SPACE_TEST_TREE_FRAME_CAPACITY];
    uint64_t tree_snapshot_addresses
        [MICROS_USER_ADDRESS_SPACE_TEST_TREE_FRAME_CAPACITY];
    size_t tree_snapshot_count;
    unsigned char user_frame_snapshots
        [MICROS_USER_ADDRESS_SPACE_TEST_USER_FRAME_CAPACITY]
        [MICROS_SV39_PAGE_SIZE];
    uint64_t user_snapshot_addresses
        [MICROS_USER_ADDRESS_SPACE_TEST_USER_FRAME_CAPACITY];
    size_t user_snapshot_count;
    volatile enum micros_user_address_space_test_sum_fault_state
        sum_fault_state;
    volatile uint64_t sum_fault_virtual_address;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_process_handle process_one;
    struct micros_process_handle process_two;
    struct micros_process_handle replacement_process;
    struct micros_thread_handle thread;
    uint64_t baseline_free;
    uint64_t baseline_owned;
    uint64_t process_one_root;
    uint64_t process_two_root;
    uint64_t replacement_root;
    uint64_t process_one_frame;
    uint64_t process_two_frame;
    uint64_t guard_frame;
    uint64_t reused_frame;
};

void micros_user_address_space_sum_fault_trigger(
    uint64_t virtual_address
);
uint64_t micros_user_address_space_sum_load(uint64_t virtual_address);

struct micros_user_address_space_test_context *
micros_user_address_space_test_context(void);

bool micros_user_address_space_test_acquire(
    struct micros_user_address_space_test_context *context
);

void micros_user_address_space_test_reset_fault(
    struct micros_user_address_space_test_context *context
);

void micros_user_address_space_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_user_address_space_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

struct micros_user_address_space_test_page_table *
micros_user_address_space_test_table_at(uint64_t physical_address);

uint64_t micros_user_address_space_test_read_satp(void);

bool micros_user_address_space_test_resolve_process_root(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t *root
);

bool micros_user_address_space_test_resolve_leaf_entry(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t **leaf_entry
);

bool micros_user_address_space_test_root_matches_kernel(
    const struct micros_user_address_space_test_context *context,
    struct micros_process_handle process,
    uint64_t *process_root
);

bool micros_user_address_space_test_snapshot_runtime(
    struct micros_user_address_space_test_context *context,
    struct micros_process_handle process,
    uint64_t virtual_address
);

bool micros_user_address_space_test_snapshot_core(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_snapshot_matches(
    const struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_page_is_zero(
    uint64_t physical_address
);

void micros_user_address_space_test_fill_page(
    uint64_t physical_address,
    uint64_t value
);

bool micros_user_address_space_test_frame_has_exact_owner(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
);

bool micros_user_address_space_test_create_roots(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_last_page_lifecycle(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_destroy_and_reuse(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_enter_handoff(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_post_handoff_revocation(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_finish_lifecycle(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_mapping_rejections(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_mapping_operations(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_reuse_page(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_validate_kernel_roots(
    struct micros_user_address_space_test_context *context
);

bool micros_user_address_space_test_reject_corruption(
    struct micros_user_address_space_test_context *context
);

#endif
