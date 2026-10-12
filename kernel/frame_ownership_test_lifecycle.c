#include "frame_ownership_test_internal.h"

bool micros_frame_ownership_test_prepare_runtime_baseline(
    struct micros_frame_ownership_test_context *context
)
{
    context->objects = micros_kernel_object_runtime_test_registry();
    context->address_space = micros_kernel_address_space_report();
    context->ledger = micros_frame_ownership_runtime_ledger();
    if (
        context->objects == NULL
        || context->address_space == NULL
        || context->ledger == NULL
        || context->ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || context->address_space->table_count == 0
        || context->ledger->owned_frame_count
            != context->address_space->table_count
        || context->ledger->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ] != context->address_space->table_count
        || micros_frame_ownership_runtime_validate(context->objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    context->baseline_table_count =
        context->address_space->table_count;
    return true;
}

bool micros_frame_ownership_test_prepare_process_generations(
    struct micros_frame_ownership_test_context *context
)
{
    if (
        micros_process_create(
            context->objects,
            &context->stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_release_process(
            context->objects,
            context->stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            context->objects,
            &context->replacement_process
        ) != MICROS_KERNEL_OBJECT_OK
        || context->replacement_process.slot
            != context->stale_process.slot
        || context->replacement_process.generation
            != context->stale_process.generation + 1
        || micros_process_create(
            context->objects,
            &context->second_process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    if (
        micros_kernel_objects_initialize(
            &context->substitute_objects,
            1,
            1
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &context->substitute_objects,
            &context->substitute_stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(
            &context->substitute_objects,
            context->substitute_stale_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &context->substitute_objects,
            &context->substitute_replacement_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(
            &context->substitute_objects,
            &context->substitute_second_process
        ) != MICROS_KERNEL_OBJECT_OK
        || context->substitute_replacement_process.slot
            != context->replacement_process.slot
        || context->substitute_replacement_process.generation
            != context->replacement_process.generation
        || context->substitute_second_process.slot
            != context->second_process.slot
        || context->substitute_second_process.generation
            != context->second_process.generation
        || micros_kernel_objects_validate(
            &context->substitute_objects
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

bool micros_frame_ownership_test_prepare_owner_records(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->stale_process,
            &context->stale_user
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            context->replacement_process,
            &context->replacement_table
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->replacement_process,
            &context->replacement_user
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            context->second_process,
            &context->second_table
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            context->second_process,
            &context->second_user
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
}

bool micros_frame_ownership_test_allocate_owned_frames(
    struct micros_frame_ownership_test_context *context
)
{
    if (
        micros_frame_ownership_runtime_allocate(
            context->replacement_table,
            &context->replacement_table_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            context->replacement_user,
            &context->replacement_user_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            context->second_table,
            &context->second_table_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            context->second_user,
            &context->second_user_frame
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    return (
        micros_frame_ownership_test_frame_index_for_address(
            context->ledger,
            context->replacement_table_frame,
            &context->replacement_table_index
        )
        && micros_frame_ownership_test_frame_index_for_address(
            context->ledger,
            context->replacement_user_frame,
            &context->replacement_user_index
        )
        && micros_frame_ownership_test_frame_index_for_address(
            context->ledger,
            context->second_table_frame,
            &context->second_table_index
        )
        && micros_frame_ownership_test_frame_index_for_address(
            context->ledger,
            context->second_user_frame,
            &context->second_user_index
        )
    );
}

bool micros_frame_ownership_test_verify_bootstrap_rejections(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        micros_frame_ownership_runtime_release(
            context->stale_user,
            context->replacement_user_frame
        ) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        && micros_frame_ownership_runtime_release(
            context->second_user,
            context->replacement_user_frame
        ) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        && micros_frame_ownership_runtime_release_process(
            context->objects,
            context->replacement_process
        ) == MICROS_KERNEL_OBJECT_ERROR_STATE
        && micros_frame_ownership_runtime_release_process(
            context->objects,
            context->second_process
        ) == MICROS_KERNEL_OBJECT_ERROR_STATE
    );
}

bool micros_frame_ownership_test_verify_owner_lookups(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        micros_frame_ownership_lookup(
            context->ledger,
            context->replacement_table_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            context->replacement_table
        )
        && micros_frame_ownership_lookup(
            context->ledger,
            context->replacement_user_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            context->replacement_user
        )
        && micros_frame_ownership_lookup(
            context->ledger,
            context->second_table_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            context->second_table
        )
        && micros_frame_ownership_lookup(
            context->ledger,
            context->second_user_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            context->second_user
        )
    );
}

static bool substitute_objects_match_snapshot(
    const struct micros_frame_ownership_test_context *context
)
{
    return micros_frame_ownership_test_bytes_equal(
        &context->substitute_objects,
        &context->substitute_state_snapshot,
        sizeof(context->substitute_objects)
    );
}

bool micros_frame_ownership_test_verify_registry_mismatch_rejections(
    struct micros_frame_ownership_test_context *context
)
{
    micros_frame_ownership_test_copy_bytes(
        &context->substitute_state_snapshot,
        &context->substitute_objects,
        sizeof(context->substitute_state_snapshot)
    );
    if (
        !micros_frame_ownership_test_snapshot_runtime_state(context)
        || micros_frame_ownership_runtime_validate(
            &context->substitute_objects
        ) != MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        || !micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
        || !substitute_objects_match_snapshot(context)
    ) {
        return false;
    }

    micros_frame_ownership_test_copy_bytes(
        &context->substitute_state_snapshot,
        &context->substitute_objects,
        sizeof(context->substitute_state_snapshot)
    );
    return (
        micros_frame_ownership_test_snapshot_runtime_state(context)
        && micros_frame_ownership_runtime_release_process(
            &context->substitute_objects,
            context->substitute_replacement_process
        ) == MICROS_KERNEL_OBJECT_ERROR_ARGUMENT
        && micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
        && substitute_objects_match_snapshot(context)
    );
}
