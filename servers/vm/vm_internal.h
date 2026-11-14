#ifndef MICROS_SERVERS_VM_INTERNAL_H
#define MICROS_SERVERS_VM_INTERNAL_H

#include <stdbool.h>

#include "lib/vm/vm_keys.h"
#include "servers/vm/vm_core.h"
#include "servers/vm/vm_admission.h"

#define MICROS_VM_INITIALIZATION_MAGIC UINT64_C(0x4d4943524f53564d)
#define MICROS_VM_PAGE_DIGEST_OFFSET UINT64_C(14695981039346656037)
#define MICROS_VM_PAGE_DIGEST_PRIME UINT64_C(1099511628211)

void vm_core_zero_bytes(void *storage, size_t size);
void vm_core_copy_bytes(void *destination, const void *source, size_t size);
bool vm_core_bytes_are_zero(const void *storage, size_t size);
bool vm_core_bytes_equal(const void *left, const void *right, size_t size);
bool vm_core_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
);
bool vm_core_endpoint_is_canonical(uint32_t endpoint);
bool vm_core_process_is_canonical(struct micros_process_handle process);
bool vm_core_user_permissions_are_canonical(uint32_t permissions);
bool vm_core_stack_plan_equal(
    const struct micros_application_stack_plan *left,
    const struct micros_application_stack_plan *right
);
bool vm_core_initial_frame_is_free(
    const struct micros_vm_state *state,
    uint64_t frame_index
);
bool vm_core_frame_availability_is_valid(const struct micros_vm_state *state, uint64_t frame_index);
bool vm_core_allocator_metadata_is_valid(const struct micros_vm_state *state);
uint64_t vm_core_load_page_digest(const struct micros_vm_state *state);
bool vm_core_physical_to_frame_index(
    const struct micros_vm_state *state,
    uint64_t physical_address,
    uint64_t *frame_index
);
bool vm_core_frame_index_to_physical(
    const struct micros_vm_state *state,
    uint64_t frame_index,
    uint64_t *physical_address
);
bool vm_core_record_is_zero(const struct micros_vm_mapping_record *record);
bool vm_core_record_contains_physical(
    const struct micros_vm_mapping_record *record,
    uint64_t physical_address
);
struct vm_core_membership_view {
    struct micros_vm_key_view keys;
    size_t group_count;
};

_Static_assert(sizeof(struct vm_core_membership_view) <= 64,
               "VM membership borrows must remain bounded");

enum micros_vm_core_error vm_core_membership_begin(
    const struct micros_vm_state *state,
    struct vm_core_membership_view *view
);
enum micros_vm_core_error vm_core_membership_query(
    const struct vm_core_membership_view *view,
    uint64_t physical_address,
    size_t ignored_slot,
    bool *present
);
bool vm_core_membership_end(struct vm_core_membership_view *view);
enum micros_vm_core_error vm_core_reserve_frames(
    struct micros_vm_state *state,
    size_t data_count,
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT],
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
);
void vm_core_release_frame_prevalidated(struct micros_vm_state *state, uint64_t frame_index);
bool vm_core_transaction_frames_match(const struct micros_vm_state *state);
enum micros_vm_core_error vm_core_validate_state(
    const struct micros_vm_state *state,
    bool allow_staged_idle_inputs
);
bool vm_core_record_header_is_valid(const struct micros_vm_state *state, size_t slot);
bool vm_core_selected_record_is_valid(const struct micros_vm_state *state, size_t slot);
enum micros_vm_core_error vm_core_validate_selected_load_plan(const struct micros_vm_state *state);

#endif
