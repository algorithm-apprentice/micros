#include "kernel/scheduler/scheduler_runtime_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/scheduler_context.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/panic.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static struct scheduler_runtime_context runtime_state;

struct scheduler_runtime_context *scheduler_runtime_state(void)
{
    return &runtime_state;
}

enum micros_scheduler_error scheduler_runtime_map_object_error(
    enum micros_kernel_object_error error
)
{
    switch (error) {
    case MICROS_KERNEL_OBJECT_OK:
        return MICROS_SCHEDULER_OK;
    case MICROS_KERNEL_OBJECT_ERROR_ARGUMENT:
    case MICROS_KERNEL_OBJECT_ERROR_POLICY:
        return MICROS_SCHEDULER_ERROR_ARGUMENT;
    case MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED:
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    case MICROS_KERNEL_OBJECT_ERROR_EMPTY:
        return MICROS_SCHEDULER_ERROR_EMPTY;
    case MICROS_KERNEL_OBJECT_ERROR_STATE:
    case MICROS_KERNEL_OBJECT_ERROR_STALE:
        return MICROS_SCHEDULER_ERROR_STATE;
    case MICROS_KERNEL_OBJECT_ERROR_STACK:
        return MICROS_SCHEDULER_ERROR_CONTEXT;
    default:
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
}

struct micros_kernel_objects *scheduler_runtime_authoritative_objects(void)
{
    return micros_kernel_object_runtime_authoritative_registry();
}

void scheduler_runtime_copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    unsigned char *destination_bytes = (unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;
    size_t index;

    for (index = 0; index < sizeof(*destination); ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

void scheduler_runtime_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

enum micros_scheduler_error scheduler_runtime_validate_selected_thread(
    struct scheduler_runtime_context *runtime,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread_handle
)
{
    const struct micros_thread *thread;
    const struct micros_process *process;

    if (
        objects == NULL
        || micros_thread_resolve(
            objects,
            thread_handle,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !thread->scheduler_assigned
        || !thread->context_attached
        || micros_process_resolve(
            objects,
            thread->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || process->address_space_root == 0
        || micros_user_address_space_validate(thread->owner)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_SCHEDULER_ERROR_CONTEXT;
    }
    runtime->selected_root = process->address_space_root;
    return MICROS_SCHEDULER_OK;
}

enum micros_scheduler_error scheduler_runtime_load_selected_context(
    struct scheduler_runtime_context *runtime,
    struct micros_thread_handle thread
)
{
    return (
        micros_user_execution_inspect(
            thread,
            &runtime->selected_context
        ) == MICROS_USER_EXECUTION_OK
        && micros_user_execution_validate_context(
            thread,
            &runtime->selected_context
        ) == MICROS_USER_EXECUTION_OK
    )
        ? MICROS_SCHEDULER_OK
        : MICROS_SCHEDULER_ERROR_CONTEXT;
}

enum micros_scheduler_error scheduler_runtime_preflight_kernel_interval(
    const struct micros_hart *hart,
    struct scheduler_accounting_commit *accounting
)
{
    uint64_t counter;
    uint64_t delta;

    if (
        hart == NULL
        || accounting == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    counter = riscv_read_time();
    delta = counter - hart->accounting_started_at;
    if (UINT64_MAX - hart->kernel_counter_ticks < delta) {
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
    accounting->counter = counter;
    accounting->kernel_total = hart->kernel_counter_ticks + delta;
    return MICROS_SCHEDULER_OK;
}

void scheduler_runtime_activate_prevalidated_root(
    struct micros_hart *hart,
    uint64_t root,
    struct micros_trap_frame *frame
)
{
    if (
        micros_riscv_activate_sv39(root)
            == (MICROS_RISCV_SATP_MODE_SV39 | (root >> 12))
    ) {
        return;
    }
    if (frame != NULL) {
        MICROS_TRAP_PANIC(
            hart == NULL ? 0 : hart->hardware_id,
            "scheduler-satp-commit",
            frame
        );
    }
    MICROS_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "scheduler-satp-commit"
    );
}

void scheduler_runtime_enter_selected_thread(
    struct scheduler_runtime_context *runtime,
    struct micros_hart *hart,
    struct micros_thread_handle selected,
    const struct scheduler_accounting_commit *accounting,
    struct micros_trap_frame *frame
)
{
    scheduler_runtime_activate_prevalidated_root(hart, runtime->selected_root, frame);
    if (frame != NULL) {
        micros_user_execution_install_return_frame(
            frame,
            &runtime->selected_context,
            hart
        );
    }
    hart->kernel_counter_ticks = accounting->kernel_total;
    hart->accounting_started_at = accounting->counter;
    hart->accounting_owner = MICROS_SCHEDULER_ACCOUNTING_THREAD;
    hart->accounted_thread = selected;
#ifdef MICROS_BUILD_SCHEDULER_TEST
    if (
        frame != NULL
        && !micros_scheduler_test_after_user_return(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-return-test",
            frame
        );
    }
#endif
}

_Noreturn void scheduler_runtime_panic_scheduler_invariant(
    struct micros_hart *hart,
    const char *reason,
    struct micros_trap_frame *frame
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}
