#include "kernel/tests/execution/user_execution_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

static bool frame_has_user_origin(
    const struct micros_trap_frame *frame,
    uint64_t cause,
    uint64_t sepc
)
{
    return (
        frame != NULL
        && (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->scause & MICROS_SCAUSE_CODE_MASK) == cause
        && frame->sepc == sepc
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) == 0
        && frame->hart_context != 0
    );
}

static bool inspect_matches_frame(
    const struct micros_user_execution_test_context *context,
    const struct micros_trap_frame *frame
)
{
    struct micros_user_context observed;

    return (
        micros_user_execution_inspect(
            context->test_thread,
            &observed
        ) == MICROS_USER_EXECUTION_OK
        && micros_user_execution_test_bytes_equal(
            &observed,
            frame,
            sizeof(observed)
        )
    );
}

static bool frame_matches_expected_context(
    const struct micros_user_execution_test_context *context,
    const struct micros_trap_frame *frame,
    uint64_t expected_sepc,
    uint64_t expected_a0,
    uint64_t expected_a7
)
{
    struct micros_user_context expected;
    uint64_t expected_words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    uint64_t observed_words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    micros_user_execution_test_copy_bytes(
        &expected,
        &context->prepared_context,
        sizeof(expected)
    );
    expected.sepc = expected_sepc;
    expected.a0 = expected_a0;
    expected.a7 = expected_a7;
    if (
        micros_user_execution_test_bytes_equal(
            &expected,
            frame,
            sizeof(expected)
        )
    ) {
        return true;
    }
    micros_user_execution_test_copy_bytes(
        expected_words,
        &expected,
        sizeof(expected_words)
    );
    micros_user_execution_test_copy_bytes(
        observed_words,
        frame,
        sizeof(observed_words)
    );
    for (
        index = 0;
        index < sizeof(expected_words) / sizeof(expected_words[0]);
        ++index
    ) {
        if (expected_words[index] != observed_words[index]) {
            uart_write(
                "MICROS_USER_EXECUTION_CONTEXT_MISMATCH index="
            );
            uart_write_hex64(index);
            uart_write(" expected=");
            uart_write_hex64(expected_words[index]);
            uart_write(" observed=");
            uart_write_hex64(observed_words[index]);
            uart_write("\n");
            uart_flush();
            break;
        }
    }
    return false;
}

static bool current_thread_and_hart_are_valid(
    const struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart_handle,
    struct micros_hart *hart
)
{
    struct micros_thread_handle current;

    return (
        micros_hart_current_thread(
            objects,
            hart_handle,
            &current
        ) == MICROS_KERNEL_OBJECT_OK
        && current.slot == context->test_thread.slot
        && current.generation == context->test_thread.generation
        && hart == &objects->harts[hart_handle.slot]
    );
}

static bool trap_stack_is_valid(
    const struct micros_user_execution_test_context *context,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    uintptr_t frame_address = (uintptr_t)frame;

    return (
        hart->trap.primary_stack_bottom
            == context->kernel_stack_bottom
        && hart->trap.primary_stack_top == context->kernel_stack_top
        && frame_address >= context->kernel_stack_bottom
        && frame_address < context->kernel_stack_top
        && sizeof(*frame)
            <= context->kernel_stack_top - frame_address
    );
}

static bool active_root_is_valid(
    const struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    const struct micros_process *process;

    return (
        micros_process_resolve(
            objects,
            context->test_process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_execution_test_read_satp()
            == (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    );
}

static bool expected_pre_capture_context(
    const struct micros_user_execution_test_context *context,
    uint64_t *expected_sepc,
    uint64_t *expected_a0,
    uint64_t *expected_a7
)
{
    switch (context->test_state) {
    case MICROS_USER_EXECUTION_TEST_KERNEL_FAULT:
        *expected_sepc = micros_user_execution_test_user_address_of(
            micros_user_execution_payload_kernel_fault
        );
        *expected_a0 = context->prepared_context.a0;
        *expected_a7 = context->prepared_context.a7;
        return true;
    case MICROS_USER_EXECUTION_TEST_FIRST_ECALL:
        *expected_sepc = micros_user_execution_test_user_address_of(
            micros_user_execution_payload_first_ecall
        );
        *expected_a0 = context->prepared_context.a0;
        *expected_a7 = 1;
        return true;
    case MICROS_USER_EXECUTION_TEST_SECOND_ECALL:
        *expected_sepc = micros_user_execution_test_user_address_of(
            micros_user_execution_payload_second_ecall
        );
        *expected_a0 = UINT64_C(0x0000000000000abd);
        *expected_a7 = 2;
        return true;
    default:
        return false;
    }
}

static void report_pre_capture_failure(const char *reason)
{
    uart_write("MICROS_TEST_FAILURE user-execution-pre-capture-");
    uart_write(reason);
    uart_write("\n");
    uart_flush();
}

bool micros_user_execution_test_pre_capture(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_user_execution_test_context *context =
        micros_user_execution_test_context();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    uint64_t expected_sepc;
    uint64_t expected_a0;
    uint64_t expected_a7;

    if (objects == NULL || hart == NULL || frame == NULL) {
        report_pre_capture_failure("argument");
        return false;
    }
    if (
        !current_thread_and_hart_are_valid(
            context,
            objects,
            hart_handle,
            hart
        )
    ) {
        report_pre_capture_failure("current");
        return false;
    }
    if (!trap_stack_is_valid(context, hart, frame)) {
        report_pre_capture_failure("stack");
        return false;
    }
    if (!active_root_is_valid(context, objects)) {
        report_pre_capture_failure("root");
        return false;
    }
    if (
        !expected_pre_capture_context(
            context,
            &expected_sepc,
            &expected_a0,
            &expected_a7
        )
    ) {
        return false;
    }
    if (
        !frame_matches_expected_context(
            context,
            frame,
            expected_sepc,
            expected_a0,
            expected_a7
        )
    ) {
        report_pre_capture_failure("context");
        return false;
    }
    return true;
}

static bool invalid_entry_returns_are_rejected(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_trap_frame invalid_return;

    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    invalid_return.sepc =
        micros_user_execution_test_user_address_of(
            micros_user_execution_payload_resume
        ) + 1;
    if (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    invalid_return.sepc =
        MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS;
    return (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) == MICROS_USER_EXECUTION_ERROR_MAPPING
    );
}

static bool invalid_stack_returns_are_rejected(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_trap_frame invalid_return;

    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    invalid_return.sp =
        MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS
        + MICROS_SV39_PAGE_SIZE;
    if (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) != MICROS_USER_EXECUTION_ERROR_MAPPING
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    invalid_return.sp =
        MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00009000);
    if (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) != MICROS_USER_EXECUTION_ERROR_MAPPING
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    --invalid_return.sp;
    return (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) == MICROS_USER_EXECUTION_ERROR_CONTEXT
    );
}

static bool invalid_status_returns_are_rejected(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    static const struct {
        uint64_t clear_mask;
        uint64_t set_mask;
    } invalid_statuses[] = {
        {0, MICROS_RISCV_SSTATUS_SIE},
        {MICROS_RISCV_SSTATUS_SPIE, 0},
        {0, MICROS_RISCV_SSTATUS_SPP},
        {0, MICROS_USER_EXECUTION_TEST_SSTATUS_UBE},
        {0, UINT64_C(1) << 9},
        {0, UINT64_C(1) << 13},
        {0, UINT64_C(1) << 15},
        {0, MICROS_RISCV_SSTATUS_SUM},
        {0, MICROS_USER_EXECUTION_TEST_SSTATUS_MXR},
        {
            MICROS_USER_EXECUTION_TEST_SSTATUS_UXL_MASK,
            UINT64_C(1) << 32,
        },
        {0, MICROS_USER_EXECUTION_TEST_SSTATUS_SD},
    };
    struct micros_trap_frame invalid_return;
    size_t index;

    for (
        index = 0;
        index
            < sizeof(invalid_statuses) / sizeof(invalid_statuses[0]);
        ++index
    ) {
        micros_user_execution_test_copy_bytes(
            &invalid_return,
            frame,
            sizeof(invalid_return)
        );
        invalid_return.sstatus &=
            ~invalid_statuses[index].clear_mask;
        invalid_return.sstatus |= invalid_statuses[index].set_mask;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        ) {
            return false;
        }
    }
    micros_user_execution_test_copy_bytes(
        &invalid_return,
        frame,
        sizeof(invalid_return)
    );
    invalid_return.sstatus ^= UINT64_C(1) << 20;
    return (
        micros_user_execution_validate_return(
            hart,
            &invalid_return
        ) == MICROS_USER_EXECUTION_ERROR_CONTEXT
    );
}

static enum micros_user_execution_test_trap_result
handle_kernel_fault(
    struct micros_user_execution_test_context *context,
    struct micros_trap_frame *frame
)
{
    if (
        !frame_has_user_origin(
            frame,
            MICROS_USER_EXECUTION_TEST_EXCEPTION_LOAD_PAGE_FAULT,
            micros_user_execution_test_user_address_of(
                micros_user_execution_payload_kernel_fault
            )
        )
        || frame->stval != UINT64_C(0x80200000)
    ) {
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }
    frame->sepc = micros_user_execution_test_user_address_of(
        micros_user_execution_payload_after_fault
    );
    context->test_state = MICROS_USER_EXECUTION_TEST_FIRST_ECALL;
    return MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN;
}

static enum micros_user_execution_test_trap_result handle_first_ecall(
    struct micros_user_execution_test_context *context,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    if (
        !frame_has_user_origin(
            frame,
            MICROS_USER_EXECUTION_TEST_EXCEPTION_USER_ECALL,
            micros_user_execution_test_user_address_of(
                micros_user_execution_payload_first_ecall
            )
        )
        || frame->a7 != 1
        || !invalid_entry_returns_are_rejected(hart, frame)
        || !invalid_stack_returns_are_rejected(hart, frame)
        || !invalid_status_returns_are_rejected(hart, frame)
    ) {
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }
    frame->a0 = UINT64_C(0x0000000000000abc);
    frame->sepc = micros_user_execution_test_user_address_of(
        micros_user_execution_payload_resume
    );
    context->test_state = MICROS_USER_EXECUTION_TEST_SECOND_ECALL;
    return MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN;
}

static enum micros_user_execution_test_trap_result handle_second_ecall(
    struct micros_user_execution_test_context *context,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | MICROS_USER_EXECUTION_TEST_SSTATUS_UBE
        | MICROS_USER_EXECUTION_TEST_SSTATUS_VS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_FS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_XS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_MXR
        | MICROS_USER_EXECUTION_TEST_SSTATUS_SD;

    if (
        !frame_has_user_origin(
            frame,
            MICROS_USER_EXECUTION_TEST_EXCEPTION_USER_ECALL,
            micros_user_execution_test_user_address_of(
                micros_user_execution_payload_second_ecall
            )
        )
        || frame->a7 != 2
        || frame->a0 != UINT64_C(0x0000000000000abd)
        || objects == NULL
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }
    frame->sp = micros_user_execution_test_saved_state[1];
    frame->sepc =
        (uintptr_t)micros_user_execution_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    context->test_state = MICROS_USER_EXECUTION_TEST_SUPERVISOR;
    return MICROS_USER_EXECUTION_TEST_TRAP_SUPERVISOR_RETURN;
}

enum micros_user_execution_test_trap_result
micros_user_execution_handle_test_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_user_execution_test_context *context =
        micros_user_execution_test_context();

    if (context->test_state == MICROS_USER_EXECUTION_TEST_IDLE) {
        return MICROS_USER_EXECUTION_TEST_TRAP_INACTIVE;
    }
    if (
        hart == NULL
        || frame == NULL
        || !inspect_matches_frame(context, frame)
    ) {
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }
    switch (context->test_state) {
    case MICROS_USER_EXECUTION_TEST_KERNEL_FAULT:
        return handle_kernel_fault(context, frame);
    case MICROS_USER_EXECUTION_TEST_FIRST_ECALL:
        return handle_first_ecall(context, hart, frame);
    case MICROS_USER_EXECUTION_TEST_SECOND_ECALL:
        return handle_second_ecall(context, hart, frame);
    default:
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }
}
