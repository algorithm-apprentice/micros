#include "kernel/scheduler_test_internal.h"

#include "arch/riscv64/trap_context.h"
#include "micros/ipc_abi.h"
#include "micros/kernel_object_runtime.h"
#include "micros/user_execution.h"

static bool preflight_user_context(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    size_t thread_index
)
{
    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || thread_index >= 2
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !scheduler_test_user_registers_match(fixture, thread_index, hart, frame)
        || !scheduler_test_observe_completion(fixture, objects, thread_index)
    ) {
        return false;
    }
    return true;
}

static enum micros_scheduler_test_trap_action start_idle_external(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    if (
        thread_index != 0
        || !fixture->spurious_idle_bypassed
        || !fixture->idle_completion_observed
        || fixture->selector_entry_count
            != fixture->selector_entries_before_spurious + 1
        || !scheduler_test_arm_external_interrupt(fixture, TEST_EXTERNAL_IDLE)
        || micros_thread_scheduler_hold(
            objects,
            fixture->threads[0]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    fixture->idle_external_started = true;
    return MICROS_SCHEDULER_TEST_CONTINUE;
}

static enum micros_scheduler_test_trap_action resume_supervisor(
    struct scheduler_test_context *fixture,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    size_t thread_index,
    uint64_t control_mask
)
{
    if (
        thread_index != 0
        || !fixture->idle_external_completion_observed
        || fixture->selector_entry_count
            != fixture->selector_entries_before_spurious + 2
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    frame->sp = micros_scheduler_test_saved_state[1];
    frame->sepc =
        (uintptr_t)micros_scheduler_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    fixture->supervisor_returned = true;
    return MICROS_SCHEDULER_TEST_RETURN_SUPERVISOR;
}

static enum micros_scheduler_test_trap_action stage_message_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_trap_frame *frame,
    size_t thread_index
)
{
    size_t target = thread_index == 0 ? 1 : 0;

    if (
        fixture->expected_completion[target]
            != TEST_COMPLETION_NONE
        || !scheduler_test_expect_message_completion(
            fixture,
            objects,
            target
        )
    ) {
        return scheduler_test_completion_test_mismatch(1);
    }
    frame->sepc += 4;
    return MICROS_SCHEDULER_TEST_CONTINUE;
}

static enum micros_scheduler_test_trap_action stage_captured_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct micros_user_context context;

    if (
        micros_user_execution_inspect(
            fixture->threads[thread_index],
            &context
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return scheduler_test_completion_test_mismatch(2);
    }
    context.sepc += 4;
    if (
        micros_user_execution_store_context(
            fixture->threads[thread_index],
            &context
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return scheduler_test_completion_test_mismatch(3);
    }
    if (
        micros_scheduler_begin_current_ipc(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return scheduler_test_completion_test_mismatch(4);
    }
    if (
        micros_thread_runtime_flags_unset(
            objects,
            fixture->threads[thread_index],
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return scheduler_test_completion_test_mismatch(5);
    }
    if (
        !scheduler_test_expect_no_message_completion(
            fixture,
            objects,
            thread_index,
            MICROS_IPC_OK,
            TEST_COMPLETION_CAPTURED
        )
    ) {
        return scheduler_test_completion_test_mismatch(6);
    }
    if (
        micros_scheduler_commit_current_ipc(
            objects,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return scheduler_test_completion_test_mismatch(7);
    }
    return MICROS_SCHEDULER_TEST_CAPTURED_USER_RETURN;
}

static enum micros_scheduler_test_trap_action handle_user_ecall(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    size_t thread_index,
    uint64_t cause_code
)
{
    uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR
        | TEST_SSTATUS_SD;

    if (
        (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
        || cause_code != TEST_EXCEPTION_USER_ECALL
        || frame->sepc
            != scheduler_test_user_address_of(micros_scheduler_payload_ecall)
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    ++fixture->ecall_count[thread_index];
    if (fixture->idle_woke_thread && !fixture->idle_external_started) {
        return start_idle_external(fixture, objects, thread_index);
    }
    if (fixture->idle_external_observed) {
        return resume_supervisor(fixture, hart, frame, thread_index, control_mask);
    }
    if (!fixture->message_completion_observed) {
        return stage_message_completion(fixture, objects, frame, thread_index);
    }
    if (!fixture->captured_completion_observed) {
        return stage_captured_completion(fixture, objects, thread_index);
    }
    frame->sepc += 4;
    return MICROS_SCHEDULER_TEST_CONTINUE;
}

static enum micros_scheduler_test_trap_action handle_user_timer(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_trap_frame *frame,
    size_t thread_index,
    uint64_t cause_code
)
{
    if (
        (frame->scause & TEST_SCAUSE_INTERRUPT) == 0
        || cause_code != TEST_INTERRUPT_SUPERVISOR_TIMER
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    ++fixture->timer_trap_count;
    if (
        fixture->captured_completion_observed
        && !fixture->error_completion_staged
    ) {
        if (
            !scheduler_test_expect_no_message_completion(
                fixture,
                objects,
                thread_index,
                MICROS_IPC_ERROR_DEAD_ENDPOINT,
                TEST_COMPLETION_ERROR
            )
        ) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        fixture->error_completion_staged = true;
    }
    if (
        fixture->switch_count >= 6
        && fixture->error_completion_observed
        && !fixture->user_external_started
    ) {
        if (!scheduler_test_arm_external_interrupt(fixture, TEST_EXTERNAL_USER)) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        fixture->user_external_started = true;
        return MICROS_SCHEDULER_TEST_CONTINUE;
    }
    if (
        fixture->switch_count >= 6
        && fixture->error_completion_observed
        && fixture->user_external_return_observed
        && !fixture->idle_requested
    ) {
        size_t other = thread_index == 0 ? 1 : 0;

        if (
            micros_thread_scheduler_hold(
                objects,
                fixture->threads[thread_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                fixture->threads[other]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        fixture->idle_requested = true;
        fixture->selector_entries_before_spurious = fixture->selector_entry_count;
        micros_scheduler_test_idle_bypass_once = 1;
    }
    return MICROS_SCHEDULER_TEST_CONTINUE;
}

enum micros_scheduler_test_trap_action
micros_scheduler_test_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t thread_index = scheduler_test_current_thread_index(fixture, hart);
    uint64_t cause_code;
    uint64_t counter;

    if (!preflight_user_context(fixture, objects, hart, frame, thread_index)) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    cause_code = frame->scause & TEST_SCAUSE_CODE_MASK;
    counter = *(const volatile uint64_t *)(uintptr_t)
        fixture->counter_physical[thread_index];
    if (counter < fixture->previous_counter[thread_index]) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    fixture->previous_counter[thread_index] = counter;
    if (!user_timer) {
        return handle_user_ecall(
            fixture, objects, hart, frame, thread_index, cause_code
        );
    }
    return handle_user_timer(fixture, objects, frame, thread_index, cause_code);
}
