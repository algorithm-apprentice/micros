#include "kernel/memory/user_address_space_internal.h"

#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/user_execution.h"

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
    return micros_user_address_space_acquire_selected_scratch(view, lease);
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

static bool build_handoff_summary(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY]
)
{
    struct micros_frame_owner owner;
    const struct micros_process *process;
    const struct micros_thread *thread;
    size_t slot;
    size_t thread_index;
    uint64_t frame_index;

    if (ledger->managed_frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES) {
        return false;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        summary[slot] = (struct micros_user_address_space_handoff_process){0, 0, 0, 0};
    }
    for (frame_index = 0; frame_index < ledger->managed_frame_count; ++frame_index) {
        owner = ledger->owners[frame_index];
        if (owner.kind == MICROS_FRAME_OWNER_FREE || owner.slot >= MICROS_PROCESS_CAPACITY) {
            continue;
        }
        process = &objects->processes[owner.slot];
        if (process->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE && process->generation == owner.generation) {
            ++summary[owner.slot].owned_frame_count;
        }
    }
    for (thread_index = 0; thread_index < MICROS_THREAD_CAPACITY; ++thread_index) {
        thread = &objects->threads[thread_index];
        if (thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        if (thread->owner.slot >= MICROS_PROCESS_CAPACITY) {
            return false;
        }
        summary[thread->owner.slot].thread_slot = (uint16_t)thread_index;
        summary[thread->owner.slot].thread_generation = thread->generation;
        ++summary[thread->owner.slot].thread_count;
    }
    return true;
}

static enum micros_user_address_space_error
validate_handoff_processes(
    const struct micros_kernel_objects *objects,
    const struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY]
)
{
    size_t index;
    const struct micros_thread *thread;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        const struct micros_user_address_space_handoff_process *state = &summary[index];
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
                || state->thread_count != 0
                || state->owned_frame_count != 0
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            }
            continue;
        }
        if (
            record->live_thread_count != 1
            || state->thread_count != 1
            || state->thread_slot >= MICROS_THREAD_CAPACITY
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        }
        thread = &objects->threads[state->thread_slot];
        if (!thread->context_attached || thread->generation != state->thread_generation
            || thread->owner.slot != process.slot || thread->owner.generation != process.generation) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        }
        thread_handle = (struct micros_thread_handle){
            .slot = state->thread_slot,
            .generation = state->thread_generation,
        };
        execution_error = micros_user_execution_validate_context(
            thread_handle,
            &thread->user_context
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
    struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY];
    struct micros_user_address_space_bootstrap_plan plan;
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
        micros_frame_ownership_runtime_validate_metadata(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    if (!build_handoff_summary(ledger, objects, summary)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(objects, summary);
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
        error = micros_user_address_space_collect_bootstrap_handoff_locked(
            &runtime,
            &scratch,
            process,
            summary[process_index].owned_frame_count,
            &plan
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_prepare_wired_process_user_set(
            scratch.release_selection
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
    struct micros_user_address_space_handoff_process summary[MICROS_PROCESS_CAPACITY];
    struct micros_user_address_space_bootstrap_plan plan;
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
        micros_frame_ownership_runtime_validate_metadata(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    if (!build_handoff_summary(ledger, objects, summary)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(objects, summary);
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
        error = micros_user_address_space_collect_bootstrap_handoff_locked(
            &runtime,
            &scratch,
            process,
            summary[index].owned_frame_count,
            &plan
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            goto done_with_scratch;
        }
        if (!plan.wired_targets) {
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
