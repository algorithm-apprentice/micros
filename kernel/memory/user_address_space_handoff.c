#include "kernel/memory/user_address_space_internal.h"

#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/user_execution.h"

static bool scratch_bitmap_contains(
    const struct micros_user_address_space_scratch_lease *lease,
    const uint64_t *bitmap,
    uint64_t frame_index
)
{
    return micros_user_address_space_bitmap_contains(
        bitmap,
        lease->word_count,
        frame_index
    );
}

static bool scratch_bitmap_add(
    const struct micros_user_address_space_scratch_lease *lease,
    uint64_t *bitmap,
    uint64_t frame_index
)
{
    return micros_user_address_space_bitmap_add(
        bitmap,
        lease->word_count,
        frame_index
    );
}

static enum micros_user_address_space_error
require_bootstrap(
    struct micros_user_address_space_runtime_view *view,
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report
)
{
    enum micros_user_address_space_error error =
        micros_user_address_space_runtime_require_bootstrap(view);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *ledger = view->ledger;
    *objects = view->objects;
    *kernel_report = view->kernel_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error acquire_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
)
{
    return micros_user_address_space_acquire_scratch(view, lease);
}

static void release_scratch(
    struct micros_user_address_space_scratch_lease *lease
)
{
    micros_user_address_space_release_scratch(lease);
}

static enum micros_user_address_space_error
map_ownership_error(enum micros_frame_ownership_error error)
{
    return micros_user_address_space_map_ownership_error(error);
}

static bool find_one_prepared_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    struct micros_thread_handle *thread_handle
)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !micros_user_address_space_process_handles_equal(
                thread->owner,
                process
            )
        ) {
            continue;
        }
        if (!thread->context_attached) {
            return false;
        }
        *thread_handle = (struct micros_thread_handle){
            .slot = (uint16_t)index,
            .generation = thread->generation,
        };
        ++count;
    }
    return count == 1;
}

static bool process_has_no_owned_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            owner->slot == process.slot
            && owner->generation == process.generation
            && owner->kind != MICROS_FRAME_OWNER_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool reachable_user_targets_are_wired(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            scratch_bitmap_contains(
                lease,
                lease->reachable_bitmap,
                frame_index
            )
            && owner->kind == MICROS_FRAME_OWNER_PROCESS_USER
            && owner->slot == process.slot
            && owner->generation == process.generation
            && ledger->handoff_targets[frame_index]
                != MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_user_address_space_error
select_reachable_process_user_targets(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *scratch,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            !scratch_bitmap_contains(
                scratch,
                scratch->reachable_bitmap,
                frame_index
            )
            || owner->kind != MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            continue;
        }
        if (
            owner->slot != process.slot
            || owner->generation != process.generation
            || scratch_bitmap_contains(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
        if (
            !scratch_bitmap_add(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_handoff_processes(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;
        struct micros_thread_handle thread_handle;
        enum micros_user_execution_error execution_error;

        if (record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        if (record->address_space_root == 0) {
            if (
                record->live_thread_count != 0
                || record->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || !process_has_no_owned_frames(ledger, process)
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            }
            continue;
        }
        if (
            record->live_thread_count != 1
            || !find_one_prepared_thread(
                objects,
                process,
                &thread_handle
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        }
        execution_error = micros_user_execution_validate_context(
            thread_handle,
            &objects->threads[thread_handle.slot].user_context
        );
        if (execution_error != MICROS_USER_EXECUTION_OK) {
            return (
                execution_error == MICROS_USER_EXECUTION_ERROR_STATE
                || execution_error
                    == MICROS_USER_EXECUTION_ERROR_CONTEXT
                || execution_error
                    == MICROS_USER_EXECUTION_ERROR_MAPPING
            )
                ? MICROS_USER_ADDRESS_SPACE_ERROR_STATE
                : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_wired_handoff(void)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t process_index;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_bootstrap(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(ledger, objects);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    for (
        process_index = 0;
        process_index < MICROS_PROCESS_CAPACITY;
        ++process_index
    ) {
        const struct micros_process *record =
            &objects->processes[process_index];
        struct micros_process_handle process;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)process_index,
            .generation = record->generation,
        };
        error = micros_user_address_space_validate_locked(
            &runtime,
            &scratch,
            process,
            NULL
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        error = select_reachable_process_user_targets(
            ledger,
            &scratch,
            process
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_prepare_wired_process_user_set(
            scratch.release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_complete_wired_handoff(void)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_bootstrap(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(ledger, objects);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        error = micros_user_address_space_validate_locked(
            &runtime,
            &scratch,
            process,
            NULL
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        if (
            !reachable_user_targets_are_wired(
                ledger,
                &scratch,
                process
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_complete_handoff(objects)
    );

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}
