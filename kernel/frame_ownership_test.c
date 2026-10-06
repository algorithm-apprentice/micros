#include "micros/frame_ownership_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"

bool micros_frame_ownership_runtime_run_self_test(void);

static struct micros_frame_ownership ownership_state_snapshot;
static struct micros_frame_allocator allocator_state_snapshot;
static struct micros_kernel_objects object_state_snapshot;
static struct micros_kernel_objects substitute_objects;
static struct micros_kernel_objects substitute_state_snapshot;

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static bool snapshot_runtime_state(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    if (ledger == NULL || ledger->allocator == NULL || objects == NULL) {
        return false;
    }
    copy_bytes(
        &ownership_state_snapshot,
        ledger,
        sizeof(ownership_state_snapshot)
    );
    copy_bytes(
        &allocator_state_snapshot,
        ledger->allocator,
        sizeof(allocator_state_snapshot)
    );
    copy_bytes(
        &object_state_snapshot,
        objects,
        sizeof(object_state_snapshot)
    );
    return true;
}

static bool runtime_state_matches_snapshot(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    return (
        ledger != NULL
        && ledger->allocator != NULL
        && objects != NULL
        && bytes_equal(
            ledger,
            &ownership_state_snapshot,
            sizeof(ownership_state_snapshot)
        )
        && bytes_equal(
            ledger->allocator,
            &allocator_state_snapshot,
            sizeof(allocator_state_snapshot)
        )
        && bytes_equal(
            objects,
            &object_state_snapshot,
            sizeof(object_state_snapshot)
        )
    );
}

static bool owners_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
)
{
    return (
        left.generation == right.generation
        && left.slot == right.slot
        && left.kind == right.kind
        && left.reserved == right.reserved
    );
}

static bool frame_index_for_address(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    if (
        ledger == NULL
        || frame_index == NULL
        || physical_address % MICROS_FRAME_SIZE != 0
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];
        uint64_t offset;
        uint64_t index_in_range;

        if (physical_address < range->base) {
            continue;
        }
        offset = physical_address - range->base;
        index_in_range = offset / MICROS_FRAME_SIZE;
        if (
            offset % MICROS_FRAME_SIZE == 0
            && index_in_range < range->frame_count
        ) {
            *frame_index = range->bitmap_offset + index_in_range;
            return *frame_index < ledger->managed_frame_count;
        }
    }
    return false;
}

static bool frame_state_matches(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target expected_target,
    uint64_t expected_owned,
    const uint64_t *expected_counts
)
{
    size_t kind;

    if (
        ledger == NULL
        || expected_counts == NULL
        || frame_index >= ledger->managed_frame_count
        || !owners_equal(ledger->owners[frame_index], expected_owner)
        || ledger->handoff_targets[frame_index] != expected_target
        || ledger->owned_frame_count != expected_owned
    ) {
        return false;
    }
    for (kind = 0; kind < MICROS_FRAME_OWNER_KIND_COUNT; ++kind) {
        if (ledger->owner_counts[kind] != expected_counts[kind]) {
            return false;
        }
    }
    return true;
}

bool micros_frame_ownership_runtime_run_self_test(void)
{
    struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *address_space;
    const struct micros_frame_ownership *ledger;
    struct micros_process_handle stale_process;
    struct micros_process_handle replacement_process;
    struct micros_process_handle second_process;
    struct micros_process_handle substitute_stale_process;
    struct micros_process_handle substitute_replacement_process;
    struct micros_process_handle substitute_second_process;
    struct micros_frame_owner stale_user;
    struct micros_frame_owner replacement_table;
    struct micros_frame_owner replacement_user;
    struct micros_frame_owner second_table;
    struct micros_frame_owner second_user;
    struct micros_frame_owner observed;
    uint64_t replacement_table_frame;
    uint64_t replacement_user_frame;
    uint64_t second_table_frame;
    uint64_t second_user_frame;
    uint64_t replacement_table_index;
    uint64_t replacement_user_index;
    uint64_t second_table_index;
    uint64_t second_user_index;
    uint64_t baseline_table_count;
    uint64_t counts_before[MICROS_FRAME_OWNER_KIND_COUNT];
    uint64_t owned_before;
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);
    uintptr_t saved_status;
    size_t kind;
    bool passed = false;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    address_space = micros_kernel_address_space_report();
    ledger = micros_frame_ownership_runtime_ledger();
    if (
        objects == NULL
        || address_space == NULL
        || ledger == NULL
        || ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || address_space->table_count == 0
        || ledger->owned_frame_count != address_space->table_count
        || ledger->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE]
            != address_space->table_count
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }
    baseline_table_count = address_space->table_count;

    if (
        micros_process_create(objects, &stale_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || replacement_process.slot != stale_process.slot
        || replacement_process.generation
            != stale_process.generation + 1
        || micros_process_create(objects, &second_process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }
    if (
        micros_kernel_objects_initialize(
            &substitute_objects,
            1,
            1
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &substitute_objects,
            &substitute_stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(
            &substitute_objects,
            substitute_stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &substitute_objects,
            &substitute_replacement_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &substitute_objects,
            &substitute_second_process
        ) != MICROS_KERNEL_OBJECT_OK
        || substitute_replacement_process.slot
            != replacement_process.slot
        || substitute_replacement_process.generation
            != replacement_process.generation
        || substitute_second_process.slot != second_process.slot
        || substitute_second_process.generation
            != second_process.generation
        || micros_kernel_objects_validate(&substitute_objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }
    if (
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            stale_process,
            &stale_user
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            replacement_process,
            &replacement_table
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            replacement_process,
            &replacement_user
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            second_process,
            &second_table
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            second_process,
            &second_user
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    if (
        micros_frame_ownership_runtime_allocate(
            replacement_table,
            &replacement_table_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            replacement_user,
            &replacement_user_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            second_table,
            &second_table_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            second_user,
            &second_user_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }
    if (
        !frame_index_for_address(
            ledger,
            replacement_table_frame,
            &replacement_table_index
        )
        || !frame_index_for_address(
            ledger,
            replacement_user_frame,
            &replacement_user_index
        )
        || !frame_index_for_address(
            ledger,
            second_table_frame,
            &second_table_index
        )
        || !frame_index_for_address(
            ledger,
            second_user_frame,
            &second_user_index
        )
    ) {
        goto done;
    }

    if (
        micros_frame_ownership_runtime_release(
            stale_user,
            replacement_user_frame
        ) != MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        || micros_frame_ownership_runtime_release(
            second_user,
            replacement_user_frame
        ) != MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        || micros_frame_ownership_runtime_release_process(
            objects,
            replacement_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STATE
        || micros_frame_ownership_runtime_release_process(
            objects,
            second_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STATE
    ) {
        goto done;
    }

    if (
        micros_frame_ownership_lookup(
            ledger,
            replacement_table_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, replacement_table)
        || micros_frame_ownership_lookup(
            ledger,
            replacement_user_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, replacement_user)
        || micros_frame_ownership_lookup(
            ledger,
            second_table_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, second_table)
        || micros_frame_ownership_lookup(
            ledger,
            second_user_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, second_user)
    ) {
        goto done;
    }

    copy_bytes(
        &substitute_state_snapshot,
        &substitute_objects,
        sizeof(substitute_state_snapshot)
    );
    if (
        !snapshot_runtime_state(ledger, objects)
        || micros_frame_ownership_runtime_validate(
            &substitute_objects
        ) != MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        || !runtime_state_matches_snapshot(ledger, objects)
        || !bytes_equal(
            &substitute_objects,
            &substitute_state_snapshot,
            sizeof(substitute_objects)
        )
    ) {
        goto done;
    }

    copy_bytes(
        &substitute_state_snapshot,
        &substitute_objects,
        sizeof(substitute_state_snapshot)
    );
    if (
        !snapshot_runtime_state(ledger, objects)
        || micros_frame_ownership_runtime_release_process(
            &substitute_objects,
            substitute_replacement_process
        ) != MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
        || !runtime_state_matches_snapshot(ledger, objects)
        || !bytes_equal(
            &substitute_objects,
            &substitute_state_snapshot,
            sizeof(substitute_objects)
        )
    ) {
        goto done;
    }

    owned_before = ledger->owned_frame_count;
    for (kind = 0; kind < MICROS_FRAME_OWNER_KIND_COUNT; ++kind) {
        counts_before[kind] = ledger->owner_counts[kind];
    }
    if (
        micros_frame_ownership_runtime_prepare_handoff(
            replacement_user_frame,
            second_user,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) != MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        || !frame_state_matches(
            ledger,
            replacement_user_index,
            replacement_user,
            MICROS_FRAME_HANDOFF_NONE,
            owned_before,
            counts_before
        )
        || micros_frame_ownership_runtime_prepare_handoff(
            replacement_table_frame,
            replacement_table,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) != MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        || !frame_state_matches(
            ledger,
            replacement_table_index,
            replacement_table,
            MICROS_FRAME_HANDOFF_NONE,
            owned_before,
            counts_before
        )
    ) {
        goto done;
    }

    if (
        micros_frame_ownership_runtime_prepare_handoff(
            replacement_user_frame,
            replacement_user,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_prepare_handoff(
            second_user_frame,
            second_user,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    copy_bytes(
        &substitute_state_snapshot,
        &substitute_objects,
        sizeof(substitute_state_snapshot)
    );
    if (
        !snapshot_runtime_state(ledger, objects)
        || micros_frame_ownership_runtime_complete_handoff(
            &substitute_objects
        ) != MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        || !runtime_state_matches_snapshot(ledger, objects)
        || !bytes_equal(
            &substitute_objects,
            &substitute_state_snapshot,
            sizeof(substitute_objects)
        )
        || micros_frame_ownership_runtime_complete_handoff(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    if (
        micros_frame_ownership_lookup(
            ledger,
            replacement_table_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, replacement_table)
        || micros_frame_ownership_lookup(
            ledger,
            second_table_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(observed, second_table)
        || micros_frame_ownership_lookup(
            ledger,
            replacement_user_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(
            observed,
            (struct micros_frame_owner){
                replacement_process.generation,
                replacement_process.slot,
                MICROS_FRAME_OWNER_VM_WIRED,
                0,
            }
        )
        || micros_frame_ownership_lookup(
            ledger,
            second_user_frame,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owners_equal(
            observed,
            (struct micros_frame_owner){
                0,
                0,
                MICROS_FRAME_OWNER_VM_TRANSFERABLE,
                0,
            }
        )
        || ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || ledger->handoff_targets[replacement_table_index]
            != MICROS_FRAME_HANDOFF_NONE
        || ledger->handoff_targets[replacement_user_index]
            != MICROS_FRAME_HANDOFF_NONE
        || ledger->handoff_targets[second_table_index]
            != MICROS_FRAME_HANDOFF_NONE
        || ledger->handoff_targets[second_user_index]
            != MICROS_FRAME_HANDOFF_NONE
        || address_space->table_count != baseline_table_count
        || ledger->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE]
            != baseline_table_count
        || ledger->owner_counts[MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE]
            != 2
        || ledger->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] != 1
        || ledger->owner_counts[MICROS_FRAME_OWNER_VM_TRANSFERABLE]
            != 1
        || ledger->owned_frame_count != baseline_table_count + 4
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_allocate(
            replacement_user,
            &unchanged
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || unchanged != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_release(
            replacement_table,
            replacement_table_frame
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_prepare_handoff(
            replacement_table_frame,
            replacement_table,
            MICROS_FRAME_HANDOFF_NONE
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_complete_handoff(objects)
            != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_release_process(
            objects,
            replacement_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STATE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (!snapshot_runtime_state(ledger, objects)) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_release_process(
            objects,
            second_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STATE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    passed = true;

done:
    riscv_irq_restore(saved_status);
    return passed;
}
