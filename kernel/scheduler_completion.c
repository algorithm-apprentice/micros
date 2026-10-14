#include "scheduler_runtime_internal.h"

#include "micros/ipc_abi.h"
#include "micros/ipc_runtime.h"

static enum scheduler_completion_error
preflight_selected_completion(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle selected,
    struct scheduler_completion_commit *completion
)
{
    const struct micros_endpoint_registry *registry;
    const struct micros_thread *resolved;
    enum micros_ipc_buffer_error buffer_error;

    if (
        objects == NULL
        || completion == NULL
        || micros_thread_resolve(objects, selected, &resolved)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return SCHEDULER_COMPLETION_INVARIANT;
    }
    scheduler_runtime_clear_bytes(completion, sizeof(*completion));
    if (!resolved->ipc_delivery_pending) {
        return micros_thread_ipc_state_is_clear(resolved)
            ? SCHEDULER_COMPLETION_OK
            : SCHEDULER_COMPLETION_INVARIANT;
    }
    registry = micros_ipc_runtime_registry();
    if (
        registry == NULL
        || micros_endpoint_registry_validate_objects(
            registry,
            objects
        ) != MICROS_ENDPOINT_OK
        || !micros_ipc_abi_map_error(
            resolved->ipc_staged_result,
            &completion->abi_result
        )
    ) {
        return SCHEDULER_COMPLETION_INVARIANT;
    }
    completion->pending = true;
    completion->thread = &objects->threads[selected.slot];
    completion->owner = resolved->owner;
    completion->receive_buffer = resolved->ipc_receive_buffer;
    completion->message = resolved->ipc_inbound_message;
    if (completion->receive_buffer == 0) {
        return SCHEDULER_COMPLETION_OK;
    }
    buffer_error = micros_ipc_buffer_prepare_write(
        completion->owner,
        completion->receive_buffer,
        &completion->buffer_plan
    );
    if (buffer_error == MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT) {
        return SCHEDULER_COMPLETION_INVALID_BUFFER;
    }
    return buffer_error == MICROS_IPC_BUFFER_OK
        ? SCHEDULER_COMPLETION_OK
        : SCHEDULER_COMPLETION_INVARIANT;
}

static enum scheduler_completion_error commit_selected_completion(
    struct scheduler_runtime_context *runtime,
    struct scheduler_completion_commit *completion
)
{
    if (completion == NULL) {
        return SCHEDULER_COMPLETION_INVARIANT;
    }
    if (!completion->pending) {
        return SCHEDULER_COMPLETION_OK;
    }
    if (completion->receive_buffer != 0) {
        micros_ipc_buffer_commit_write(
            &completion->buffer_plan,
            &completion->message
        );
    }
    runtime->selected_context.a0 = completion->abi_result;
    scheduler_runtime_copy_user_context(
        &completion->thread->user_context,
        &runtime->selected_context
    );
    completion->thread->ipc_receive_buffer = 0;
    completion->thread->ipc_delivery_pending = false;
    scheduler_runtime_clear_bytes(
        &completion->thread->ipc_inbound_message,
        sizeof(completion->thread->ipc_inbound_message)
    );
    completion->thread->ipc_staged_result = MICROS_IPC_OK;
    return SCHEDULER_COMPLETION_OK;
}

static _Noreturn void panic_completion(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum scheduler_completion_error error
)
{
    scheduler_runtime_panic_scheduler_invariant(
        hart,
        error == SCHEDULER_COMPLETION_INVALID_BUFFER
            ? "invalid-bootstrap-ipc-buffer"
            : "scheduler-ipc-completion",
        frame
    );
}

void scheduler_runtime_preflight_completion_or_panic(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle selected,
    struct scheduler_completion_commit *completion,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    enum scheduler_completion_error error =
        preflight_selected_completion(
            objects,
            selected,
            completion
        );

    if (error != SCHEDULER_COMPLETION_OK) {
        panic_completion(hart, frame, error);
    }
}

void scheduler_runtime_commit_completion_or_panic(
    struct scheduler_runtime_context *runtime,
    struct scheduler_completion_commit *completion,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    enum scheduler_completion_error error =
        commit_selected_completion(runtime, completion);

    if (error != SCHEDULER_COMPLETION_OK) {
        panic_completion(hart, frame, error);
    }
}

#ifdef MICROS_BUILD_SCHEDULER_TEST
bool micros_scheduler_test_rejects_malformed_completion(
    struct micros_thread_handle thread
)
{
    struct micros_kernel_objects *objects = scheduler_runtime_authoritative_objects();
    struct scheduler_completion_commit completion;

    return (
        objects != NULL
        && preflight_selected_completion(
            objects,
            thread,
            &completion
        ) == SCHEDULER_COMPLETION_INVARIANT
    );
}
#endif
