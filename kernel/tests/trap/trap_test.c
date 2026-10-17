#include "kernel/trap/trap_internal.h"

#include <stddef.h>

#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

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

struct trap_test_context {
    volatile enum trap_test_state trap_test_state;
    volatile uint32_t trap_test_count;
    uint64_t trap_test_entry_sstatus;
};

static struct trap_test_context test_context;

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
        && (trap_read_sstatus() & MICROS_RISCV_SSTATUS_SUM) == 0
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
        && (trap_read_sstatus() & MICROS_RISCV_SSTATUS_SUM) == 0
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

    test_context.trap_test_entry_sstatus = frame->sstatus;
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
        test_context.trap_test_entry_sstatus ^ MICROS_RISCV_SSTATUS_SUM
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

bool trap_handle_supervisor_test(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_TRAP_TEST
    if (test_context.trap_test_state == TRAP_TEST_ARMED) {
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

        ++test_context.trap_test_count;
        test_context.trap_test_state = TRAP_TEST_HANDLED;
        trap_test_prepare_return(frame);
        return true;
    }
    if (test_context.trap_test_state == TRAP_TEST_SUM_RESTORE_ARMED) {
        if (!trap_test_has_sum_restore_exception(frame)) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "trap-test-sum-capture",
                frame
            );
        }
        ++test_context.trap_test_count;
        test_context.trap_test_state = TRAP_TEST_SUM_RESTORE_HANDLED;
        trap_test_prepare_sum_restore_return(frame);
        return true;
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
    (void)hart;
    (void)frame;
    return false;
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
    test_context.trap_test_count = 0;
    test_context.trap_test_state = TRAP_TEST_ARMED;
    micros_trap_test_trigger();
    passed = (
        test_context.trap_test_state == TRAP_TEST_HANDLED
        && test_context.trap_test_count == 1
        && trap_test_has_return_registers()
    );
    if (passed) {
        test_context.trap_test_state = TRAP_TEST_SUM_RESTORE_ARMED;
        micros_trap_sum_restore_test_trigger();
        passed = (
            test_context.trap_test_state == TRAP_TEST_SUM_RESTORE_HANDLED
            && test_context.trap_test_count == 2
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
        && trap_read_sscratch() == (uintptr_t)hart
        && trap_read_tp() == (uintptr_t)hart
        && micros_kernel_object_runtime_validate()
            == MICROS_KERNEL_OBJECT_OK
    );
    test_context.trap_test_state = TRAP_TEST_IDLE;
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
