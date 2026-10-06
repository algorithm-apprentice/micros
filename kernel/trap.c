#include "micros/trap.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MICROS_EXCEPTION_ILLEGAL_INSTRUCTION = 2,
    MICROS_INTERRUPT_SUPERVISOR_TIMER = 5,
};

extern unsigned char __trap_stack_bottom[];
extern unsigned char __trap_stack_top[];
extern unsigned char __trap_emergency_stack_bottom[];
extern unsigned char __trap_emergency_stack_top[];
extern unsigned char micros_trap_entry[];

void micros_riscv_trap_install(struct micros_hart *hart);

static uintptr_t read_sscratch(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, sscratch" : "=r"(value));
    return value;
}

static uintptr_t read_stvec(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, stvec" : "=r"(value));
    return value;
}

static uintptr_t read_tp(void)
{
    uintptr_t value;

    __asm__ volatile("mv %0, tp" : "=r"(value));
    return value;
}

#if defined(MICROS_BUILD_TRAP_TEST) \
    || defined(MICROS_BUILD_NESTED_TRAP_TEST)
static uintptr_t read_sstatus(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, sstatus" : "=r"(value));
    return value;
}
#endif

static bool frame_is_on_primary_stack(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    uintptr_t frame_address = (uintptr_t)frame;

    return (
        hart != NULL
        && hart->trap_installed
        && frame_address >= hart->trap.primary_stack_bottom
        && frame_address < hart->trap.primary_stack_top
        && sizeof(*frame)
            <= hart->trap.primary_stack_top - frame_address
    );
}

static struct micros_hart *resolve_trap_hart(
    const struct micros_trap_frame *frame
)
{
    struct micros_hart *hart;

    if (frame == NULL || frame->hart_context == 0) {
        return NULL;
    }
    hart = micros_kernel_object_runtime_hart_from_context(
        frame->hart_context
    );
    if (
        hart == NULL
        || !frame_is_on_primary_stack(hart, frame)
        || read_sscratch() != 0
        || read_tp() != (uintptr_t)hart
    ) {
        return NULL;
    }
    return hart;
}

static void handle_scheduler_timer_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_scheduler_error error
)
{
    switch (error) {
    case MICROS_SCHEDULER_OK:
        return;
    case MICROS_SCHEDULER_ERROR_TIMER_INACTIVE:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "unexpected-timer",
            frame
        );
    case MICROS_SCHEDULER_ERROR_TIMER_TICK_OVERFLOW:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "timer-tick-overflow",
            frame
        );
    case MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "timer-rearm-failed",
            frame
        );
    default:
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-timer",
            frame
        );
    }
}

bool micros_trap_install(void)
{
    struct micros_hart *hart;

    if (
        micros_kernel_object_runtime_install_trap_stacks(
            (uintptr_t)__trap_stack_bottom,
            (uintptr_t)__trap_stack_top,
            (uintptr_t)__trap_emergency_stack_bottom,
            (uintptr_t)__trap_emergency_stack_top
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    hart = micros_kernel_object_runtime_boot_hart();
    if (hart == NULL) {
        return false;
    }
    micros_riscv_trap_install(hart);
    return (
        read_sscratch() == (uintptr_t)hart
        && read_tp() == (uintptr_t)hart
        && read_stvec() == (uintptr_t)micros_trap_entry
    );
}

#if defined(MICROS_BUILD_TRAP_TEST) \
    || defined(MICROS_BUILD_TRAP_PANIC_TEST)
void micros_trap_test_trigger(void);
void micros_trap_panic_test_trigger(void);

extern const unsigned char micros_trap_test_fault[];
extern const unsigned char micros_trap_test_resume[];
extern const unsigned char micros_trap_panic_test_fault[];
#endif

#ifdef MICROS_BUILD_TRAP_TEST
void micros_trap_sum_restore_test_trigger(void);

extern const unsigned char micros_trap_test_entry_stack_top[];
extern const unsigned char micros_trap_test_return_sp[];
extern const uint64_t micros_trap_test_snapshot[32];
extern const unsigned char micros_trap_sum_restore_test_fault[];
extern const unsigned char micros_trap_sum_restore_test_resume[];
extern const uint64_t micros_trap_sum_restore_observed_status;

enum trap_test_state {
    TRAP_TEST_IDLE,
    TRAP_TEST_ARMED,
    TRAP_TEST_HANDLED,
    TRAP_TEST_SUM_RESTORE_ARMED,
    TRAP_TEST_SUM_RESTORE_HANDLED,
};

static volatile enum trap_test_state trap_test_state;
static volatile uint32_t trap_test_count;
static uint64_t trap_test_entry_sstatus;

static bool trap_test_has_entry_registers(
    const struct micros_trap_frame *frame
)
{
#define MICROS_CHECK_ENTRY(field, number) \
    if (frame->field != UINT64_C(0x100) + (number)) { \
        return false; \
    }

    MICROS_CHECK_ENTRY(ra, 1);
    if (
        frame->sp
        != (uintptr_t)micros_trap_test_entry_stack_top
    ) {
        return false;
    }
    MICROS_CHECK_ENTRY(gp, 3);
    MICROS_CHECK_ENTRY(tp, 4);
    MICROS_CHECK_ENTRY(t0, 5);
    MICROS_CHECK_ENTRY(t1, 6);
    MICROS_CHECK_ENTRY(t2, 7);
    MICROS_CHECK_ENTRY(s0, 8);
    MICROS_CHECK_ENTRY(s1, 9);
    MICROS_CHECK_ENTRY(a0, 10);
    MICROS_CHECK_ENTRY(a1, 11);
    MICROS_CHECK_ENTRY(a2, 12);
    MICROS_CHECK_ENTRY(a3, 13);
    MICROS_CHECK_ENTRY(a4, 14);
    MICROS_CHECK_ENTRY(a5, 15);
    MICROS_CHECK_ENTRY(a6, 16);
    MICROS_CHECK_ENTRY(a7, 17);
    MICROS_CHECK_ENTRY(s2, 18);
    MICROS_CHECK_ENTRY(s3, 19);
    MICROS_CHECK_ENTRY(s4, 20);
    MICROS_CHECK_ENTRY(s5, 21);
    MICROS_CHECK_ENTRY(s6, 22);
    MICROS_CHECK_ENTRY(s7, 23);
    MICROS_CHECK_ENTRY(s8, 24);
    MICROS_CHECK_ENTRY(s9, 25);
    MICROS_CHECK_ENTRY(s10, 26);
    MICROS_CHECK_ENTRY(s11, 27);
    MICROS_CHECK_ENTRY(t3, 28);
    MICROS_CHECK_ENTRY(t4, 29);
    MICROS_CHECK_ENTRY(t5, 30);
    MICROS_CHECK_ENTRY(t6, 31);

#undef MICROS_CHECK_ENTRY
    return true;
}

static bool trap_test_has_expected_exception(
    const struct micros_trap_frame *frame
)
{
    return (
        (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->scause & MICROS_SCAUSE_CODE_MASK)
            == MICROS_EXCEPTION_ILLEGAL_INSTRUCTION
        && frame->sepc == (uintptr_t)micros_trap_test_fault
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SIE) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) != 0
        && (read_sstatus() & MICROS_RISCV_SSTATUS_SUM) == 0
        && frame->hart_context != 0
    );
}

static bool trap_test_has_sum_restore_exception(
    const struct micros_trap_frame *frame
)
{
    return (
        (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->scause & MICROS_SCAUSE_CODE_MASK)
            == MICROS_EXCEPTION_ILLEGAL_INSTRUCTION
        && frame->sepc
            == (uintptr_t)micros_trap_sum_restore_test_fault
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SIE) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) == 0
        && (read_sstatus() & MICROS_RISCV_SSTATUS_SUM) == 0
        && frame->hart_context != 0
    );
}

static void trap_test_prepare_return(struct micros_trap_frame *frame)
{
#define MICROS_SET_RETURN(field, number) \
    frame->field = UINT64_C(0x200) + (number)

    MICROS_SET_RETURN(ra, 1);
    frame->sp = (uintptr_t)micros_trap_test_return_sp;
    MICROS_SET_RETURN(gp, 3);
    MICROS_SET_RETURN(tp, 4);
    MICROS_SET_RETURN(t0, 5);
    MICROS_SET_RETURN(t1, 6);
    MICROS_SET_RETURN(t2, 7);
    MICROS_SET_RETURN(s0, 8);
    MICROS_SET_RETURN(s1, 9);
    MICROS_SET_RETURN(a0, 10);
    MICROS_SET_RETURN(a1, 11);
    MICROS_SET_RETURN(a2, 12);
    MICROS_SET_RETURN(a3, 13);
    MICROS_SET_RETURN(a4, 14);
    MICROS_SET_RETURN(a5, 15);
    MICROS_SET_RETURN(a6, 16);
    MICROS_SET_RETURN(a7, 17);
    MICROS_SET_RETURN(s2, 18);
    MICROS_SET_RETURN(s3, 19);
    MICROS_SET_RETURN(s4, 20);
    MICROS_SET_RETURN(s5, 21);
    MICROS_SET_RETURN(s6, 22);
    MICROS_SET_RETURN(s7, 23);
    MICROS_SET_RETURN(s8, 24);
    MICROS_SET_RETURN(s9, 25);
    MICROS_SET_RETURN(s10, 26);
    MICROS_SET_RETURN(s11, 27);
    MICROS_SET_RETURN(t3, 28);
    MICROS_SET_RETURN(t4, 29);
    MICROS_SET_RETURN(t5, 30);
    MICROS_SET_RETURN(t6, 31);

#undef MICROS_SET_RETURN

    trap_test_entry_sstatus = frame->sstatus;
    frame->sstatus ^= MICROS_RISCV_SSTATUS_SUM;
    frame->sstatus &= ~MICROS_RISCV_SSTATUS_SIE;
    frame->sepc = (uintptr_t)micros_trap_test_resume;
}

static bool trap_test_has_return_registers(void)
{
    size_t index;
    uint64_t expected_status;
    uint64_t observed_status;

    for (index = 1; index <= 31; ++index) {
        uint64_t expected = UINT64_C(0x200) + index;

        if (index == 2) {
            expected = (uintptr_t)micros_trap_test_return_sp;
        }
        if (micros_trap_test_snapshot[index - 1] != expected) {
            return false;
        }
    }

    expected_status = (
        trap_test_entry_sstatus ^ MICROS_RISCV_SSTATUS_SUM
    );
    observed_status = micros_trap_test_snapshot[31];
    return (
        (observed_status & MICROS_RISCV_SSTATUS_SUM)
            == (expected_status & MICROS_RISCV_SSTATUS_SUM)
        && (observed_status & MICROS_RISCV_SSTATUS_SIE) == 0
        && (observed_status & MICROS_RISCV_SSTATUS_SPIE) != 0
        && (observed_status & MICROS_RISCV_SSTATUS_SPP) == 0
    );
}

static void trap_test_prepare_sum_restore_return(
    struct micros_trap_frame *frame
)
{
    frame->sstatus |= MICROS_RISCV_SSTATUS_SUM;
    frame->sstatus &= ~MICROS_RISCV_SSTATUS_SIE;
    frame->sepc = (uintptr_t)micros_trap_sum_restore_test_resume;
}
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
static bool trap_panic_test_has_expected_exception(
    const struct micros_trap_frame *frame
)
{
    return (
        (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->scause & MICROS_SCAUSE_CODE_MASK)
            == MICROS_EXCEPTION_ILLEGAL_INSTRUCTION
        && frame->sepc == (uintptr_t)micros_trap_panic_test_fault
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        && frame->hart_context != 0
    );
}
#endif

void micros_trap_dispatch(struct micros_trap_frame *frame)
{
    struct micros_hart *hart = resolve_trap_hart(frame);
    uint64_t cause_code;

    if (hart == NULL) {
        struct micros_hart *boot_hart =
            micros_kernel_object_runtime_boot_hart();
        uintptr_t hart_id =
            boot_hart == NULL ? 0 : boot_hart->hardware_id;

        MICROS_TRAP_PANIC(
            hart_id,
            "trap-route-invalid",
            frame
        );
    }
    cause_code = frame->scause & MICROS_SCAUSE_CODE_MASK;

    if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0) {
        bool user_timer = (
            (frame->scause & MICROS_SCAUSE_INTERRUPT) != 0
            && cause_code == MICROS_INTERRUPT_SUPERVISOR_TIMER
        );

        if (
            micros_scheduler_user_trap_enter(hart, frame)
                != MICROS_SCHEDULER_OK
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "scheduler-user-entry",
                frame
            );
        }
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
        enum micros_user_execution_test_trap_result result;

        if (!micros_user_execution_test_pre_capture(hart, frame)) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "user-execution-pre-capture",
                frame
            );
        }
#endif
        if (
            micros_user_execution_capture_trap(hart, frame)
                != MICROS_USER_EXECUTION_OK
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "user-execution-capture",
                frame
            );
        }
        if (user_timer) {
            handle_scheduler_timer_error(
                hart,
                frame,
                micros_scheduler_handle_user_timer(hart)
            );
        }
#ifdef MICROS_BUILD_SCHEDULER_SWITCH_TEST
        {
            enum micros_scheduler_switch_test_trap_action action =
                micros_scheduler_switch_test_handle_user_trap(
                    hart,
                    frame,
                    user_timer
                );

            if (
                action
                    == MICROS_SCHEDULER_SWITCH_TEST_RETURN_SUPERVISOR
            ) {
                return;
            }
            if (
                action != MICROS_SCHEDULER_SWITCH_TEST_CONTINUE
            ) {
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "scheduler-switch-test-mismatch",
                    frame
                );
            }
        }
#elif defined(MICROS_BUILD_USER_EXECUTION_TEST)
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
        result = micros_user_execution_handle_test_trap(
            hart,
            frame
        );
        if (
            result
                == MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN
        ) {
            if (
                micros_scheduler_select_user_return(hart, frame)
                    != MICROS_SCHEDULER_OK
            ) {
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "scheduler-user-return",
                    frame
                );
            }
            return;
        }
        if (
            result
                == MICROS_USER_EXECUTION_TEST_TRAP_SUPERVISOR_RETURN
        ) {
            return;
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-execution-test-mismatch",
            frame
        );
#endif
#else
        if (!user_timer) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "unexpected-user-trap",
                frame
            );
        }
#endif
        if (
            micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "scheduler-user-return",
                frame
            );
        }
        return;
    }

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    {
        enum micros_user_address_space_test_trap_result result =
            micros_user_address_space_handle_test_trap(frame);

        if (
            result
                == MICROS_USER_ADDRESS_SPACE_TEST_TRAP_HANDLED
        ) {
            return;
        }
        if (
            result
                == MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "user-address-space-test-mismatch",
                frame
            );
        }
    }
#endif

#ifdef MICROS_BUILD_MMU_TEST
    {
        enum micros_mmu_test_trap_result result =
            micros_kernel_address_space_handle_test_trap(frame);

        if (result == MICROS_MMU_TEST_TRAP_HANDLED) {
            return;
        }
        if (result == MICROS_MMU_TEST_TRAP_MISMATCH) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "mmu-test-mismatch",
                frame
            );
        }
    }
#endif

#ifdef MICROS_BUILD_TRAP_TEST
    if (trap_test_state == TRAP_TEST_ARMED) {
        if (
            !trap_test_has_expected_exception(frame)
            || !trap_test_has_entry_registers(frame)
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "trap-test-capture",
                frame
            );
        }

        ++trap_test_count;
        trap_test_state = TRAP_TEST_HANDLED;
        trap_test_prepare_return(frame);
        return;
    }
    if (trap_test_state == TRAP_TEST_SUM_RESTORE_ARMED) {
        if (!trap_test_has_sum_restore_exception(frame)) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "trap-test-sum-capture",
                frame
            );
        }
        ++trap_test_count;
        trap_test_state = TRAP_TEST_SUM_RESTORE_HANDLED;
        trap_test_prepare_sum_restore_return(frame);
        return;
    }
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
    if (!trap_panic_test_has_expected_exception(frame)) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "trap-test-mismatch",
            frame
        );
    }
#endif

    if ((frame->scause & MICROS_SCAUSE_INTERRUPT) != 0) {
        if (cause_code == MICROS_INTERRUPT_SUPERVISOR_TIMER) {
            enum micros_timer_interrupt_result result =
                micros_timer_handle_interrupt(hart);

            switch (result) {
            case MICROS_TIMER_INTERRUPT_HANDLED:
            case MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS:
                return;
            case MICROS_TIMER_INTERRUPT_INACTIVE:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "unexpected-timer",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_TICK_OVERFLOW:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "timer-tick-overflow",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_REARM_FAILED:
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "timer-rearm-failed",
                    frame
                );
            }
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "timer-result-invalid",
                frame
            );
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "unexpected-interrupt",
            frame
        );
    }
    MICROS_TRAP_PANIC(
        hart->hardware_id,
        "unexpected-exception",
        frame
    );
}

_Noreturn void micros_trap_nested_panic(
    struct micros_hart *hart,
    const struct micros_trap_frame *outer_frame
)
{
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
    const uint64_t poison_tp = UINT64_C(0x4e45535445445450);
    struct micros_hart *resolved =
        micros_kernel_object_runtime_hart_from_context(
            (uintptr_t)hart
        );
    uintptr_t stack_probe = (uintptr_t)&resolved;

    if (
        resolved == NULL
        || resolved != hart
        || outer_frame == NULL
        || !frame_is_on_primary_stack(resolved, outer_frame)
        || outer_frame->hart_context != (uintptr_t)resolved
        || outer_frame->tp != poison_tp
        || (
            outer_frame->sstatus
            & MICROS_RISCV_SSTATUS_SUM
        ) == 0
        || (read_sstatus() & MICROS_RISCV_SSTATUS_SUM) != 0
        || stack_probe < resolved->trap.emergency_stack_bottom
        || stack_probe >= resolved->trap.emergency_stack_top
        || read_sscratch() != 0
        || read_tp() != (uintptr_t)resolved
    ) {
        struct micros_hart *boot_hart =
            micros_kernel_object_runtime_boot_hart();
        uintptr_t hart_id =
            boot_hart == NULL ? 0 : boot_hart->hardware_id;

        MICROS_PANIC(hart_id, "nested-trap-test-route");
    }
    uart_write(
        "MICROS_NESTED_TRAP_TEST_PASS "
        "hart=routed emergency-stack=selected\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
#else
    struct micros_hart *resolved =
        micros_kernel_object_runtime_hart_from_context(
            (uintptr_t)hart
        );
    struct micros_hart *boot_hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id;

    (void)outer_frame;
    if (resolved != NULL) {
        hart_id = resolved->hardware_id;
    } else if (boot_hart != NULL) {
        hart_id = boot_hart->hardware_id;
    } else {
        hart_id = 0;
    }
    MICROS_PANIC(hart_id, "nested-trap");
#endif
}

#ifdef MICROS_BUILD_TRAP_TEST
bool micros_trap_run_self_test(void)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    bool passed;

    if (hart == NULL) {
        return false;
    }
    trap_test_count = 0;
    trap_test_state = TRAP_TEST_ARMED;
    micros_trap_test_trigger();
    passed = (
        trap_test_state == TRAP_TEST_HANDLED
        && trap_test_count == 1
        && trap_test_has_return_registers()
    );
    if (passed) {
        trap_test_state = TRAP_TEST_SUM_RESTORE_ARMED;
        micros_trap_sum_restore_test_trigger();
        passed = (
            trap_test_state == TRAP_TEST_SUM_RESTORE_HANDLED
            && trap_test_count == 2
            && (
                micros_trap_sum_restore_observed_status
                & MICROS_RISCV_SSTATUS_SUM
            ) != 0
            && (
                micros_trap_sum_restore_observed_status
                & MICROS_RISCV_SSTATUS_SIE
            ) == 0
        );
    }
    passed = (
        passed
        && read_sscratch() == (uintptr_t)hart
        && read_tp() == (uintptr_t)hart
        && micros_kernel_object_runtime_validate()
            == MICROS_KERNEL_OBJECT_OK
    );
    trap_test_state = TRAP_TEST_IDLE;
    return passed;
}
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
_Noreturn void micros_trap_run_panic_test(void)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();

    micros_trap_panic_test_trigger();
    MICROS_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        "trap-test-returned"
    );
}
#endif
