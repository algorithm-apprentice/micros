#include "micros/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "scheduler_core_internal.h"

static bool scheduler_initialized;
static bool scheduler_timer_enabled;
static uint64_t scheduler_preemption_interval;
static struct micros_user_context selected_context;
static uint64_t selected_root;

struct scheduler_accounting_commit {
    uint64_t counter;
    uint64_t kernel_total;
};

_Noreturn void micros_riscv_enter_user(
    const struct micros_user_context *context
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
        || micros_user_execution_inspect(
            thread_handle,
            &selected_context
        ) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_validate_context(
            thread_handle,
            &selected_context
        ) != MICROS_USER_EXECUTION_OK
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
#ifdef MICROS_BUILD_SCHEDULER_SWITCH_TEST
    if (
        frame != NULL
        && !micros_scheduler_switch_test_after_user_return(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-switch-return-test",
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
    uintptr_t saved_status = riscv_irq_save();
    enum micros_kernel_object_error error;

    if (
        preemption_interval == 0
        || scheduler_initialized
        || objects == NULL
        || hart == NULL
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

enum micros_scheduler_error micros_scheduler_start(void)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
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
    scheduler_error = preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        riscv_irq_restore(saved_status);
        return scheduler_error;
    }
    if (
        scheduler_timer_enabled
        && !micros_timer_start(
            hart,
            scheduler_preemption_interval
        )
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_SCHEDULER_ERROR_TIMER;
    }
    scheduler_error = preflight_kernel_interval(hart, &accounting);
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-start-accounting"
        );
    }
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        NULL
    );
#ifdef MICROS_BUILD_SCHEDULER_SWITCH_TEST
    if (!micros_scheduler_switch_test_after_start(hart)) {
        MICROS_PANIC(
            hart->hardware_id,
            "scheduler-switch-start-test"
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
    if (!scheduler_initialized || hart == NULL) {
        return MICROS_SCHEDULER_ERROR_NOT_INITIALIZED;
    }
    return handle_timer_result(
        micros_timer_handle_interrupt(hart)
    );
}

enum micros_scheduler_error micros_scheduler_select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects = authoritative_objects();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_scheduler_return_plan plan;
    struct scheduler_accounting_commit accounting;
    struct micros_user_context outgoing;
    struct micros_thread_handle current;
    enum micros_kernel_object_error error;
    enum micros_scheduler_error scheduler_error;

    if (
        !scheduler_initialized
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || micros_hart_current_thread(
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
        || micros_user_execution_store_context(current, &outgoing)
            != MICROS_USER_EXECUTION_OK
    ) {
        return MICROS_SCHEDULER_ERROR_CONTEXT;
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
        return MICROS_SCHEDULER_ERROR_IDLE;
    }
    scheduler_error = validate_selected_thread(
        objects,
        plan.selected
    );
    if (scheduler_error != MICROS_SCHEDULER_OK) {
        return scheduler_error;
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
    micros_scheduler_apply_return_plan(objects, &plan);
    enter_selected_thread(
        hart,
        plan.selected,
        &accounting,
        frame
    );
    return MICROS_SCHEDULER_OK;
}

#ifdef MICROS_BUILD_USER_EXECUTION_TEST
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
    || defined(MICROS_BUILD_SCHEDULER_SWITCH_TEST)
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
