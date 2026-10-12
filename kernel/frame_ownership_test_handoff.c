#include "frame_ownership_test_internal.h"

bool micros_frame_ownership_test_verify_handoff_rejections(
    struct micros_frame_ownership_test_context *context
)
{
    size_t kind;

    context->owned_before = context->ledger->owned_frame_count;
    for (kind = 0; kind < MICROS_FRAME_OWNER_KIND_COUNT; ++kind) {
        context->counts_before[kind] =
            context->ledger->owner_counts[kind];
    }
    return (
        micros_frame_ownership_runtime_prepare_handoff(
            context->replacement_user_frame,
            context->second_user,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER
        && micros_frame_ownership_test_frame_state_matches(
            context->ledger,
            context->replacement_user_index,
            context->replacement_user,
            MICROS_FRAME_HANDOFF_NONE,
            context->owned_before,
            context->counts_before
        )
        && micros_frame_ownership_runtime_prepare_handoff(
            context->replacement_table_frame,
            context->replacement_table,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) == MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        && micros_frame_ownership_test_frame_state_matches(
            context->ledger,
            context->replacement_table_index,
            context->replacement_table,
            MICROS_FRAME_HANDOFF_NONE,
            context->owned_before,
            context->counts_before
        )
    );
}

bool micros_frame_ownership_test_commit_handoff(
    struct micros_frame_ownership_test_context *context
)
{
    if (
        micros_frame_ownership_runtime_prepare_handoff(
            context->replacement_user_frame,
            context->replacement_user,
            MICROS_FRAME_HANDOFF_VM_WIRED
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_prepare_handoff(
            context->second_user_frame,
            context->second_user,
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_validate(context->objects)
            != MICROS_FRAME_OWNERSHIP_OK
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
        && micros_frame_ownership_runtime_complete_handoff(
            &context->substitute_objects
        ) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT
        && micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
        && micros_frame_ownership_test_bytes_equal(
            &context->substitute_objects,
            &context->substitute_state_snapshot,
            sizeof(context->substitute_objects)
        )
        && micros_frame_ownership_runtime_complete_handoff(
            context->objects
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
}

static bool handoff_frame_owners_are_exact(
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
            context->second_table_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            context->second_table
        )
        && micros_frame_ownership_lookup(
            context->ledger,
            context->replacement_user_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            (struct micros_frame_owner){
                context->replacement_process.generation,
                context->replacement_process.slot,
                MICROS_FRAME_OWNER_VM_WIRED,
                0,
            }
        )
        && micros_frame_ownership_lookup(
            context->ledger,
            context->second_user_frame,
            &context->observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_test_owners_equal(
            context->observed,
            (struct micros_frame_owner){
                0,
                0,
                MICROS_FRAME_OWNER_VM_TRANSFERABLE,
                0,
            }
        )
    );
}

bool micros_frame_ownership_test_verify_handoff_state(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        handoff_frame_owners_are_exact(context)
        && context->ledger->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        && context->ledger->handoff_targets[
            context->replacement_table_index
        ] == MICROS_FRAME_HANDOFF_NONE
        && context->ledger->handoff_targets[
            context->replacement_user_index
        ] == MICROS_FRAME_HANDOFF_NONE
        && context->ledger->handoff_targets[
            context->second_table_index
        ] == MICROS_FRAME_HANDOFF_NONE
        && context->ledger->handoff_targets[
            context->second_user_index
        ] == MICROS_FRAME_HANDOFF_NONE
        && context->address_space->table_count
            == context->baseline_table_count
        && context->ledger->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ] == context->baseline_table_count
        && context->ledger->owner_counts[
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        ] == 2
        && context->ledger->owner_counts[
            MICROS_FRAME_OWNER_VM_WIRED
        ] == 1
        && context->ledger->owner_counts[
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        ] == 1
        && context->ledger->owned_frame_count
            == context->baseline_table_count + 4
    );
}

static bool sealed_frame_mutations_are_rejected(
    struct micros_frame_ownership_test_context *context
)
{
    context->unchanged = UINT64_C(0xfeedfacefeedface);
    if (
        !micros_frame_ownership_test_snapshot_runtime_state(context)
        || micros_frame_ownership_runtime_allocate(
            context->replacement_user,
            &context->unchanged
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || context->unchanged != UINT64_C(0xfeedfacefeedface)
        || !micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    ) {
        return false;
    }
    if (
        !micros_frame_ownership_test_snapshot_runtime_state(context)
        || micros_frame_ownership_runtime_release(
            context->replacement_table,
            context->replacement_table_frame
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || !micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    ) {
        return false;
    }
    return (
        micros_frame_ownership_test_snapshot_runtime_state(context)
        && micros_frame_ownership_runtime_prepare_handoff(
            context->replacement_table_frame,
            context->replacement_table,
            MICROS_FRAME_HANDOFF_NONE
        ) == MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        && micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    );
}

static bool sealed_handoff_and_process_release_are_rejected(
    struct micros_frame_ownership_test_context *context
)
{
    if (
        !micros_frame_ownership_test_snapshot_runtime_state(context)
        || micros_frame_ownership_runtime_complete_handoff(
            context->objects
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || !micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    ) {
        return false;
    }
    if (
        !micros_frame_ownership_test_snapshot_runtime_state(context)
        || micros_frame_ownership_runtime_release_process(
            context->objects,
            context->replacement_process
        ) != MICROS_KERNEL_OBJECT_ERROR_STATE
        || !micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    ) {
        return false;
    }
    return (
        micros_frame_ownership_test_snapshot_runtime_state(context)
        && micros_frame_ownership_runtime_release_process(
            context->objects,
            context->second_process
        ) == MICROS_KERNEL_OBJECT_ERROR_STATE
        && micros_frame_ownership_test_runtime_state_matches_snapshot(
            context
        )
    );
}

bool micros_frame_ownership_test_verify_sealed_rejections(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        sealed_frame_mutations_are_rejected(context)
        && sealed_handoff_and_process_release_are_rejected(context)
    );
}

bool micros_frame_ownership_test_verify_final_state(
    struct micros_frame_ownership_test_context *context
)
{
    return (
        micros_kernel_objects_validate(context->objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_frame_ownership_runtime_validate(context->objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}
