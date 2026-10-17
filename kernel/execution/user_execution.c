#include "micros/user_execution.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/execution/user_execution_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"

extern const unsigned char __thread_kernel_stacks_start[];
extern const unsigned char __thread_kernel_stacks_end[];

_Noreturn void micros_riscv_enter_user(
    const struct micros_user_context *context
);

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static _Noreturn void panic_invariant(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id = hart == NULL ? 0 : hart->hardware_id;

    MICROS_PANIC(hart_id, reason);
}

static bool derive_stack_bounds(
    struct micros_thread_handle thread,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
)
{
    uintptr_t pool_start =
        (uintptr_t)__thread_kernel_stacks_start;
    uintptr_t pool_end = (uintptr_t)__thread_kernel_stacks_end;
    uintptr_t expected_size =
        MICROS_THREAD_CAPACITY * MICROS_THREAD_KERNEL_STACK_SIZE;
    uintptr_t bottom;

    if (
        stack_bottom == NULL
        || stack_top == NULL
        || thread.slot >= MICROS_THREAD_CAPACITY
        || pool_end < pool_start
        || pool_end - pool_start != expected_size
        || pool_start % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return false;
    }
    bottom = pool_start
        + (thread.slot * MICROS_THREAD_KERNEL_STACK_SIZE);
    *stack_bottom = bottom;
    *stack_top = bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    return (
        *stack_bottom % MICROS_SV39_PAGE_SIZE == 0
        && *stack_top % MICROS_TRAP_STACK_ALIGNMENT == 0
        && *stack_top <= pool_end
    );
}

static void clear_stack(uintptr_t stack_bottom)
{
    uint64_t *words = (uint64_t *)stack_bottom;
    size_t index;

    for (
        index = 0;
        index < MICROS_THREAD_KERNEL_STACK_SIZE / sizeof(uint64_t);
        ++index
    ) {
        words[index] = 0;
    }
}

static enum micros_user_execution_error
validate_post_handoff_thread(
    struct micros_kernel_objects *objects,
    struct micros_process_handle process_handle,
    struct micros_thread_handle thread_handle
)
{
    const struct micros_process *process;
    uint32_t next_generation;
    enum micros_kernel_object_error object_error;
    enum micros_user_execution_error error;

    object_error = micros_process_resolve(
        objects,
        process_handle,
        &process
    );
    if (object_error != MICROS_KERNEL_OBJECT_OK) {
        return micros_user_execution_map_object_error(object_error);
    }
    if (
        process->live_thread_count != 0
        || thread_handle.slot >= MICROS_THREAD_CAPACITY
        || objects->threads[thread_handle.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
    ) {
        return MICROS_USER_EXECUTION_ERROR_STATE;
    }
    object_error = micros_thread_next_generation(
        objects->threads[thread_handle.slot].generation,
        &next_generation
    );
    if (
        object_error == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
    ) {
        return MICROS_USER_EXECUTION_ERROR_STATE;
    }
    error = micros_user_execution_map_object_error(object_error);
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    return next_generation == thread_handle.generation
        ? MICROS_USER_EXECUTION_OK
        : MICROS_USER_EXECUTION_ERROR_STALE;
}

static enum micros_user_execution_error
prepare_post_handoff_binding(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *context,
    struct micros_user_execution_binding *candidate
)
{
    const struct micros_hart *hart;
    struct micros_hart_handle hart_handle;

    clear_bytes(candidate, sizeof(*candidate));
    candidate->thread = thread_handle;
    hart_handle = micros_kernel_object_runtime_boot_hart_handle();
    if (
        micros_hart_resolve(objects, hart_handle, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || hart_handle.slot != 0
        || hart->hardware_id != 0
        || !derive_stack_bounds(
            thread_handle,
            &candidate->kernel_stack_bottom,
            &candidate->kernel_stack_top
        )
    ) {
        return MICROS_USER_EXECUTION_ERROR_STACK;
    }
    candidate->hart = hart_handle;
    copy_bytes(
        &candidate->context,
        context,
        sizeof(candidate->context)
    );
    return micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        ? MICROS_USER_EXECUTION_OK
        : MICROS_USER_EXECUTION_ERROR_INVARIANT;
}

enum micros_user_execution_error
micros_user_execution_preflight_post_handoff(
    struct micros_process_handle process_handle,
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *initial_context,
    struct micros_user_execution_binding *binding
)
{
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_user_execution_binding candidate;
    struct micros_user_context prepared;
    enum micros_user_execution_error error;
    uintptr_t saved_status;

    if (
        initial_context == NULL
        || binding == NULL
        || (uintptr_t)binding
            % _Alignof(struct micros_user_execution_binding)
            != 0
    ) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    ledger = micros_frame_ownership_runtime_ledger();
    if (ledger == NULL) {
        error = MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
        goto done;
    }
    if (
        ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_EXECUTION_ERROR_PHASE;
        goto done;
    }
    error = validate_post_handoff_thread(
        objects,
        process_handle,
        thread_handle
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    error = micros_user_execution_prepare_post_handoff_context(
        process_handle,
        initial_context,
        &prepared
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    error = prepare_post_handoff_binding(
        objects,
        thread_handle,
        &prepared,
        &candidate
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    copy_bytes(binding, &candidate, sizeof(candidate));

done:
    riscv_irq_restore(saved_status);
    return error;
}

void micros_user_execution_commit_post_handoff_prevalidated(
    const struct micros_user_execution_binding *binding
)
{
    clear_stack(binding->kernel_stack_bottom);
}

static enum micros_user_execution_error require_bootstrap_thread(
    struct micros_thread_handle thread_handle,
    struct micros_kernel_objects **objects,
    const struct micros_thread **thread
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_process *process;
    enum micros_user_execution_error error;

    error = micros_user_execution_resolve_thread_and_process(
        thread_handle,
        objects,
        thread,
        &process
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        return error;
    }
    ledger = micros_frame_ownership_runtime_ledger();
    if (ledger == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return MICROS_USER_EXECUTION_ERROR_PHASE;
    }
    if (
        (*thread)->runtime_flags != MICROS_THREAD_RTS_INACTIVE
        || (*thread)->scheduler_assigned
        || (*thread)->context_attached
    ) {
        return MICROS_USER_EXECUTION_ERROR_STATE;
    }
    (void)process;
    return MICROS_USER_EXECUTION_OK;
}

static enum micros_user_execution_error prepare_kernel_stack(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
)
{
    if (
        !derive_stack_bounds(
            thread_handle,
            stack_bottom,
            stack_top
        )
    ) {
        return MICROS_USER_EXECUTION_ERROR_STACK;
    }
    return micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        ? MICROS_USER_EXECUTION_OK
        : MICROS_USER_EXECUTION_ERROR_INVARIANT;
}

static enum micros_user_execution_error commit_execution_context(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle,
    uintptr_t stack_bottom,
    uintptr_t stack_top,
    const struct micros_user_context *prepared
)
{
    enum micros_user_execution_error error;

    clear_stack(stack_bottom);
    error = micros_user_execution_map_object_error(
        micros_thread_attach_execution_context(
            objects,
            thread_handle,
            stack_bottom,
            stack_top,
            prepared
        )
    );
    if (
        error == MICROS_USER_EXECUTION_OK
        && micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        if (
            micros_thread_detach_execution_context(
                objects,
                thread_handle
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_invariant("user-context-prepare-rollback");
        }
        return MICROS_USER_EXECUTION_ERROR_INVARIANT;
    }
    return error;
}

enum micros_user_execution_error micros_user_execution_prepare(
    struct micros_thread_handle thread_handle,
    const struct micros_user_context *initial_context
)
{
    struct micros_kernel_objects *objects;
    const struct micros_thread *thread;
    struct micros_user_context prepared;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    enum micros_user_execution_error error;
    uintptr_t saved_status;

    if (initial_context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = require_bootstrap_thread(
        thread_handle,
        &objects,
        &thread
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    error = micros_user_execution_prepare_bootstrap_context(
        thread->owner,
        initial_context,
        &prepared
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    error = prepare_kernel_stack(
        objects,
        thread_handle,
        &stack_bottom,
        &stack_top
    );
    if (error != MICROS_USER_EXECUTION_OK) {
        goto done;
    }
    error = commit_execution_context(
        objects,
        thread_handle,
        stack_bottom,
        stack_top,
        &prepared
    );

done:
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_execution_error micros_user_execution_inspect(
    struct micros_thread_handle thread,
    struct micros_user_context *context
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (context == NULL) {
        return MICROS_USER_EXECUTION_ERROR_ARGUMENT;
    }
    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_inspect_execution_context(
        objects,
        thread,
        context,
        &stack_bottom,
        &stack_top
    );
    riscv_irq_restore(saved_status);
    return micros_user_execution_map_object_error(error);
}

enum micros_user_execution_error micros_user_execution_detach(
    struct micros_thread_handle thread
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    enum micros_kernel_object_error error;
    uintptr_t saved_status;

    if (objects == NULL) {
        return MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    error = micros_thread_detach_execution_context(objects, thread);
    if (
        error == MICROS_KERNEL_OBJECT_OK
        && micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_KERNEL_OBJECT_ERROR_INVARIANT;
    }
    riscv_irq_restore(saved_status);
    return micros_user_execution_map_object_error(error);
}

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
bool micros_user_execution_test_stack_bounds(
    struct micros_thread_handle thread,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    const struct micros_thread *resolved;

    return (
        objects != NULL
        && stack_bottom != NULL
        && stack_top != NULL
        && micros_thread_resolve(objects, thread, &resolved)
            == MICROS_KERNEL_OBJECT_OK
        && derive_stack_bounds(thread, stack_bottom, stack_top)
    );
}
#endif
