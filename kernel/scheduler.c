#include "micros/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/scheduler_context.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_buffer_internal.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "scheduler_core_internal.h"

static bool scheduler_initialized;
static bool scheduler_timer_enabled;
static bool scheduler_start_timer_prepared;
static uint64_t scheduler_preemption_interval;
static struct micros_user_context selected_context;
static uint64_t selected_root;
static uint64_t scheduler_kernel_root;

struct scheduler_accounting_commit {
    uint64_t counter;
    uint64_t kernel_total;
};

enum scheduler_completion_error {
    SCHEDULER_COMPLETION_OK = 0,
    SCHEDULER_COMPLETION_INVALID_BUFFER,
    SCHEDULER_COMPLETION_INVARIANT,
};

struct scheduler_completion_commit {
    bool pending;
    struct micros_thread *thread;
    struct micros_process_handle owner;
    uintptr_t receive_buffer;
    struct micros_ipc_message message;
    struct micros_ipc_buffer_plan buffer_plan;
    uint64_t abi_result;
};

_Noreturn void micros_riscv_enter_user(
    const struct micros_user_context *context
);
_Noreturn void micros_scheduler_idle_pivot(struct micros_hart *hart);
void micros_scheduler_idle_select(struct micros_hart *hart);
_Noreturn void micros_scheduler_idle_accounting_panic(
    struct micros_hart *hart
);

static enum micros_scheduler_error map_object_error(
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

static struct micros_kernel_objects *authoritative_objects(void)
{
    return micros_kernel_object_runtime_authoritative_registry();
}

static void copy_user_context(
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

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

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
    clear_bytes(completion, sizeof(*completion));
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
    selected_context.a0 = completion->abi_result;
    copy_user_context(
        &completion->thread->user_context,
        &selected_context
    );
    completion->thread->ipc_receive_buffer = 0;
    completion->thread->ipc_delivery_pending = false;
    clear_bytes(
        &completion->thread->ipc_inbound_message,
        sizeof(completion->thread->ipc_inbound_message)
    );
    completion->thread->ipc_staged_result = MICROS_IPC_OK;
    return SCHEDULER_COMPLETION_OK;
}

static enum micros_scheduler_error validate_selected_thread(
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
    selected_root = process->address_space_root;
    return MICROS_SCHEDULER_OK;
}

static enum micros_scheduler_error load_selected_context(
    struct micros_thread_handle thread
)
{
    return (
        micros_user_execution_inspect(
            thread,
            &selected_context
        ) == MICROS_USER_EXECUTION_OK
        && micros_user_execution_validate_context(
            thread,
            &selected_context
        ) == MICROS_USER_EXECUTION_OK
    )
        ? MICROS_SCHEDULER_OK
        : MICROS_SCHEDULER_ERROR_CONTEXT;
}

static _Noreturn void panic_invalid_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool outgoing
)
{
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    if (!micros_scheduler_invalid_test_report(
        hart,
        frame,
        outgoing
    )) {
        MICROS_TRAP_PANIC(
            hart == NULL ? 0 : hart->hardware_id,
            "scheduler-invalid-context-diagnostic",
            frame
        );
    }
#else
    (void)outgoing;
#endif
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "invalid-bootstrap-user-context",
        frame
    );
}

static _Noreturn void panic_scheduler_invariant(
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

static _Noreturn void panic_completion(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum scheduler_completion_error error
)
{
    panic_scheduler_invariant(
        hart,
        error == SCHEDULER_COMPLETION_INVALID_BUFFER
            ? "invalid-bootstrap-ipc-buffer"
            : "scheduler-ipc-completion",
        frame
    );
}

static void preflight_completion_or_panic(
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

static void commit_completion_or_panic(
    struct scheduler_completion_commit *completion,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    enum scheduler_completion_error error =
        commit_selected_completion(completion);

    if (error != SCHEDULER_COMPLETION_OK) {
        panic_completion(hart, frame, error);
    }
}

static enum micros_scheduler_error preflight_kernel_interval(
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

static void activate_prevalidated_root(
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

static void enter_selected_thread(
    struct micros_hart *hart,
    struct micros_thread_handle selected,
    const struct scheduler_accounting_commit *accounting,
    struct micros_trap_frame *frame
)
{
    activate_prevalidated_root(hart, selected_root, frame);
    if (frame != NULL) {
        micros_user_execution_install_return_frame(
            frame,
            &selected_context,
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

enum micros_scheduler_error micros_scheduler_initialize(
    uint64_t preemption_interval
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;

    if (
        preemption_interval == 0
        || scheduler_initialized
        || objects == NULL
        || hart == NULL
        || kernel_report == NULL
        || kernel_report->root_physical_address == 0
        || hart->current_thread.generation != 0
    ) {
        riscv_irq_restore(saved_status);
        return preemption_interval == 0
            ? MICROS_SCHEDULER_ERROR_ARGUMENT
            : MICROS_SCHEDULER_ERROR_STATE;
    }
    if (!micros_timer_initialize(hart)) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    error = micros_scheduler_accounting_initialize(
        objects,
        hart_handle,
        riscv_read_time()
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        riscv_irq_restore(saved_status);
        return map_object_error(error);
    }
    scheduler_preemption_interval = preemption_interval;
    scheduler_kernel_root = kernel_report->root_physical_address;
    scheduler_timer_enabled = true;
    scheduler_initialized = true;
    riscv_irq_restore(saved_status);
    return MICROS_SCHEDULER_OK;
}

bool micros_scheduler_is_initialized(void)
{
    return scheduler_initialized;
}

enum micros_scheduler_error micros_scheduler_admit(
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart =
        micros_kernel_object_runtime_boot_hart_handle();
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;

    if (!scheduler_initialized || objects == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        micros_user_execution_inspect(
            thread,
            &selected_context
        ) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_validate_context(
            thread,
            &selected_context
        ) != MICROS_USER_EXECUTION_OK
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_CONTEXT;
    }
    error = micros_thread_scheduler_admit(
        objects,
        hart,
        thread,
        priority,
        quantum_counter_ticks,
        true
    );
    riscv_irq_restore(saved_status);
    return map_object_error(error);
}

enum micros_scheduler_error
micros_scheduler_prepare_start_timer(void)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t saved_status = riscv_irq_save();

    if (
        !scheduler_initialized
        || hart == NULL
        || !scheduler_timer_enabled
        || scheduler_start_timer_prepared
        || hart->current_thread.generation != 0
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    if (
        !micros_timer_start(
            hart,
            scheduler_preemption_interval
        )
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    scheduler_start_timer_prepared = true;
    riscv_irq_restore(saved_status);
    return MICROS_SCHEDULER_OK;
}

enum micros_scheduler_error micros_scheduler_start(void)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct scheduler_completion_commit completion;
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;
    enum micros_scheduler_error scheduler_error;

    if (!scheduler_initialized || objects == NULL || hart == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (hart->current_thread.generation != 0) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (
        error != MICROS_KERNEL_OBJECT_OK
        || plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE
    ) {
        riscv_irq_restore(saved_status);
        return error == MICROS_KERNEL_OBJECT_OK
            ? MICROS_SCHEDULER_ERROR_EMPTY
            : map_object_error(error);
    }
    scheduler_error = validate_selected_thread(
        objects,
        plan.selected
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        NULL
    );
    scheduler_error = load_selected_context(plan.selected);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    scheduler_error = preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    if (scheduler_timer_enabled) {
        if (
            scheduler_start_timer_prepared
            && !micros_timer_prepare_return(hart)
        ) {
            riscv_irq_restore(saved_status);
            return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
        }
        if (
            !scheduler_start_timer_prepared
            && !micros_timer_start(
                hart,
                scheduler_preemption_interval
            )
        ) {
            riscv_irq_restore(saved_status);
            return MICROS_SCHEDULER_ERROR_TIMER;
        }
    }
    scheduler_error = preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-start-accounting"
        );
    }
    commit_completion_or_panic(&completion, hart, NULL);
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        NULL
    );
#ifdef MICROS_BUILD_SCHEDULER_TEST
    if (!micros_scheduler_test_after_start(hart)) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-start-test"
        );
    }
#endif
    (void)saved_status;
    micros_riscv_enter_user(&selected_context);
}

enum micros_scheduler_error micros_scheduler_user_trap_enter(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    enum micros_kernel_object_error error;

    if (
        !scheduler_initialized
        || objects == NULL
        || hart == NULL
        || frame == NULL
    ) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        || hart != &objects->harts[hart_handle.slot]
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    error = micros_scheduler_account_user_trap(
        objects,
        hart_handle,
        riscv_read_time()
    );
    return map_object_error(error);
}

static enum micros_scheduler_error handle_timer_result(
    enum micros_timer_interrupt_result result
)
{
    switch (result) {
    case MICROS_TIMER_INTERRUPT_HANDLED:
    case MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS:
        return MICROS_SCHEDULER_OK;
    case MICROS_TIMER_INTERRUPT_INACTIVE:
        return MICROS_SCHEDULER_ERROR_TIMER_INACTIVE;
    case MICROS_TIMER_INTERRUPT_TICK_OVERFLOW:
        return MICROS_SCHEDULER_ERROR_TIMER_TICK_OVERFLOW;
    case MICROS_TIMER_INTERRUPT_REARM_FAILED:
        return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
    }
    return MICROS_SCHEDULER_ERROR_INVARIANT;
}

enum micros_scheduler_error micros_scheduler_handle_user_timer(
    struct micros_hart *hart
)
{
    enum micros_scheduler_error error;

    if (!scheduler_initialized || hart == NULL) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    error = handle_timer_result(
        micros_timer_handle_interrupt(hart)
    );
    if (error == MICROS_SCHEDULER_OK) {
        micros_bootstrap_runtime_check_deadline(riscv_read_time());
    }
    return error;
}

static enum micros_scheduler_error select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool store_outgoing
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct scheduler_completion_commit completion;
    struct micros_user_context outgoing;
    struct micros_thread_handle current = {0, 0};
    enum micros_kernel_object_error error;
    enum micros_scheduler_error scheduler_error;

    if (
        !scheduler_initialized
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || hart != &objects->harts[hart_handle.slot]
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    if (store_outgoing) {
        if (
            micros_hart_current_thread(
                objects,
                hart_handle,
                &current
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_SCHEDULER_ERROR_STATE;
        }
        copy_user_context(
            &outgoing,
            (const struct micros_user_context *)frame
        );
        if (
            micros_user_execution_validate_context(current, &outgoing)
                != MICROS_USER_EXECUTION_OK
        ) {
            panic_invalid_context(hart, frame, true);
        }
        if (
            micros_user_execution_store_context(current, &outgoing)
                != MICROS_USER_EXECUTION_OK
        ) {
            panic_scheduler_invariant(
                hart,
                "scheduler-outgoing-store",
                frame
            );
        }
    } else if (
        hart->current_thread.slot != 0
        || hart->current_thread.generation != 0
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        return map_object_error(error);
    }
    if (plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE) {
        if (
            scheduler_timer_enabled
            && !micros_timer_prepare_return(hart)
        ) {
            return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
        }
        scheduler_error = preflight_kernel_interval(
            hart,
            &accounting
        );
        if (scheduler_error != MICROS_SCHEDULER_OK) {
            return scheduler_error;
        }
        micros_scheduler_apply_return_plan(objects, &plan);
        activate_prevalidated_root(
            hart,
            scheduler_kernel_root,
            frame
        );
        hart->kernel_counter_ticks = accounting.kernel_total;
        hart->accounting_started_at = accounting.counter;
        hart->accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_IDLE;
        hart->accounted_thread =
            (struct micros_thread_handle){0, 0};
        micros_scheduler_idle_pivot(hart);
    }

    scheduler_error = validate_selected_thread(
        objects,
        plan.selected
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        panic_invalid_context(hart, frame, false);
    }
    preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        frame
    );
    scheduler_error = load_selected_context(plan.selected);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        panic_invalid_context(hart, frame, false);
    }
    if (
        scheduler_timer_enabled
        && !micros_timer_prepare_return(hart)
    ) {
        return MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED;
    }
    scheduler_error = preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
    }
    commit_completion_or_panic(&completion, hart, frame);
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        frame
    );
    return MICROS_SCHEDULER_OK;
}

enum micros_scheduler_error micros_scheduler_select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    return select_user_return(hart, frame, true);
}

enum micros_scheduler_error
micros_scheduler_select_captured_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    return select_user_return(hart, frame, false);
}

#ifdef MICROS_BUILD_SCHEDULER_TEST
bool micros_scheduler_test_rejects_malformed_completion(
    struct micros_thread_handle thread
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
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

enum micros_scheduler_error micros_scheduler_handle_supervisor_timer(
    struct micros_hart *hart
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    enum micros_timer_interrupt_result timer_result;
    enum micros_kernel_object_error error;

    if (!scheduler_initialized || objects == NULL || hart == NULL) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    if (
        hart->accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_IDLE
    ) {
        error = micros_scheduler_account_idle_trap(
            objects,
            hart_handle,
            riscv_read_time()
        );
        if (error != MICROS_KERNEL_OBJECT_OK) {
            return map_object_error(error);
        }
    } else if (
        hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
    timer_result = micros_timer_handle_interrupt(hart);
    if (
        timer_result == MICROS_TIMER_INTERRUPT_HANDLED
        || timer_result == MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS
    ) {
        micros_bootstrap_runtime_check_deadline(riscv_read_time());
    }
    if (timer_result == MICROS_TIMER_INTERRUPT_HANDLED) {
        hart->reschedule_pending = true;
#ifdef MICROS_BUILD_SCHEDULER_TEST
        if (
            hart->current_thread.generation == 0
            && !micros_scheduler_test_handle_idle_timer(hart)
        ) {
            return MICROS_SCHEDULER_ERROR_INVARIANT;
        }
#endif
    }
    return handle_timer_result(timer_result);
}

void micros_scheduler_idle_select(struct micros_hart *hart)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct scheduler_completion_commit completion;
    enum micros_kernel_object_error error;

#ifdef MICROS_BUILD_SCHEDULER_TEST
    micros_scheduler_test_note_selector_entry();
#endif
    if (
        objects == NULL
        || hart == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    error = micros_hart_plan_user_return(
        objects,
        hart_handle,
        &plan
    );
    if (error != MICROS_KERNEL_OBJECT_OK) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    if (plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE) {
        if (
            (
                scheduler_timer_enabled
                && !micros_timer_prepare_return(hart)
            )
            || preflight_kernel_interval(hart, &accounting)
                != MICROS_SCHEDULER_OK
        ) {
            micros_scheduler_idle_accounting_panic(hart);
        }
        micros_scheduler_apply_return_plan(objects, &plan);
        hart->kernel_counter_ticks = accounting.kernel_total;
        hart->accounting_started_at = accounting.counter;
        hart->accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_IDLE;
        hart->accounted_thread =
            (struct micros_thread_handle){0, 0};
        return;
    }
    if (
        validate_selected_thread(
            objects,
            plan.selected
        ) != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    preflight_completion_or_panic(
        objects,
        plan.selected,
        &completion,
        hart,
        NULL
    );
    if (
        load_selected_context(plan.selected)
            != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    if (
        (
            scheduler_timer_enabled
            && !micros_timer_prepare_return(hart)
        )
        || preflight_kernel_interval(hart, &accounting)
            != MICROS_SCHEDULER_OK
    ) {
        micros_scheduler_idle_accounting_panic(hart);
    }
    commit_completion_or_panic(&completion, hart, NULL);
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        NULL
    );
    micros_riscv_enter_user(&selected_context);
}

_Noreturn void micros_scheduler_idle_accounting_panic(
    struct micros_hart *hart
)
{
    MICROS_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "scheduler-idle-accounting"
    );
}

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_GRANT_SYSCALL_TEST)
_Noreturn void micros_scheduler_test_enter_without_timer(
    struct micros_thread_handle thread
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    uintptr_t saved_status = riscv_irq_save();

    if (
        scheduler_initialized
        || objects == NULL
        || hart == NULL
        || micros_scheduler_accounting_initialize(
            objects,
            hart_handle,
            riscv_read_time()
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        MICROS_PANIC(
            hart == NULL ? 0 : hart->hardware_id,
            "scheduler-test-init"
        );
    }
    scheduler_initialized = true;
    if (
        micros_thread_scheduler_admit(
            objects,
            hart_handle,
            thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX,
            true
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || validate_selected_thread(
            objects,
            plan.selected
        ) != MICROS_SCHEDULER_OK
        || load_selected_context(plan.selected)
            != MICROS_SCHEDULER_OK
        || preflight_kernel_interval(hart, &accounting)
            != MICROS_SCHEDULER_OK
    ) {
        MICROS_PANIC(hart->hardware_id, "scheduler-test-entry");
    }
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        NULL
    );
    (void)saved_status;
    micros_riscv_enter_user(&selected_context);
}
#endif

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_TEST) \
    || defined(MICROS_BUILD_GRANT_SYSCALL_TEST) \
    || defined(MICROS_BUILD_USER_RUNTIME_TEST)
enum micros_scheduler_error
micros_scheduler_test_prepare_supervisor_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle current;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || micros_hart_current_thread(
            objects,
            hart_handle,
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_SCHEDULER_ERROR_STATE;
    }
    if (
        scheduler_timer_enabled
        && hart->timer.active
        && !micros_timer_stop(hart)
    ) {
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    if (
        micros_thread_scheduler_hold(objects, current)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_plan_user_return(
            objects,
            hart_handle,
            &plan
        ) != MICROS_KERNEL_OBJECT_OK
        || plan.action != MICROS_SCHEDULER_RETURN_ENTER_IDLE
        || micros_hart_commit_user_return(objects, &plan)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_SCHEDULER_ERROR_INVARIANT;
    }
    return MICROS_SCHEDULER_OK;
}
#endif
