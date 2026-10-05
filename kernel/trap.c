#include "micros/trap.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_address_space.h"
#include "micros/panic.h"
#include "micros/timer.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MICROS_EXCEPTION_ILLEGAL_INSTRUCTION = 2,
    MICROS_INTERRUPT_SUPERVISOR_TIMER = 5,
};

uintptr_t micros_trap_hart_id;

#if defined(MICROS_BUILD_TRAP_TEST) \
    || defined(MICROS_BUILD_TRAP_PANIC_TEST)
void micros_trap_test_trigger(void);
void micros_trap_panic_test_trigger(void);

extern const unsigned char micros_trap_test_fault[];
extern const unsigned char micros_trap_test_resume[];
extern const unsigned char micros_trap_panic_test_fault[];
#endif

#ifdef MICROS_BUILD_TRAP_TEST
extern const unsigned char micros_trap_test_entry_stack_top[];
extern const unsigned char micros_trap_test_return_sp[];
extern const uint64_t micros_trap_test_snapshot[32];

enum trap_test_state {
    TRAP_TEST_IDLE,
    TRAP_TEST_ARMED,
    TRAP_TEST_HANDLED,
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
        && frame->reserved == 0
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
        && frame->reserved == 0
    );
}
#endif

void micros_trap_dispatch(struct micros_trap_frame *frame)
{
    uint64_t cause_code = frame->scause & MICROS_SCAUSE_CODE_MASK;

#ifdef MICROS_BUILD_MMU_TEST
    {
        enum micros_mmu_test_trap_result result =
            micros_kernel_address_space_handle_test_trap(frame);

        if (result == MICROS_MMU_TEST_TRAP_HANDLED) {
            return;
        }
        if (result == MICROS_MMU_TEST_TRAP_MISMATCH) {
            MICROS_TRAP_PANIC(
                micros_trap_hart_id,
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
                micros_trap_hart_id,
                "trap-test-capture",
                frame
            );
        }

        ++trap_test_count;
        trap_test_state = TRAP_TEST_HANDLED;
        trap_test_prepare_return(frame);
        return;
    }
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
    if (!trap_panic_test_has_expected_exception(frame)) {
        MICROS_TRAP_PANIC(
            micros_trap_hart_id,
            "trap-test-mismatch",
            frame
        );
    }
#endif

    if ((frame->scause & MICROS_SCAUSE_INTERRUPT) != 0) {
        if (cause_code == MICROS_INTERRUPT_SUPERVISOR_TIMER) {
            enum micros_timer_interrupt_result result =
                micros_timer_handle_interrupt();

            switch (result) {
            case MICROS_TIMER_INTERRUPT_HANDLED:
            case MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS:
                return;
            case MICROS_TIMER_INTERRUPT_INACTIVE:
                MICROS_TRAP_PANIC(
                    micros_trap_hart_id,
                    "unexpected-timer",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_TICK_OVERFLOW:
                MICROS_TRAP_PANIC(
                    micros_trap_hart_id,
                    "timer-tick-overflow",
                    frame
                );
            case MICROS_TIMER_INTERRUPT_REARM_FAILED:
                MICROS_TRAP_PANIC(
                    micros_trap_hart_id,
                    "timer-rearm-failed",
                    frame
                );
            }
            MICROS_TRAP_PANIC(
                micros_trap_hart_id,
                "timer-result-invalid",
                frame
            );
        }
        MICROS_TRAP_PANIC(
            micros_trap_hart_id,
            "unexpected-interrupt",
            frame
        );
    }
    MICROS_TRAP_PANIC(
        micros_trap_hart_id,
        "unexpected-exception",
        frame
    );
}

_Noreturn void micros_trap_nested_panic(void)
{
    MICROS_PANIC(micros_trap_hart_id, "nested-trap");
}

#ifdef MICROS_BUILD_TRAP_TEST
bool micros_trap_run_self_test(void)
{
    bool passed;

    trap_test_count = 0;
    trap_test_state = TRAP_TEST_ARMED;
    micros_trap_test_trigger();
    passed = (
        trap_test_state == TRAP_TEST_HANDLED
        && trap_test_count == 1
        && trap_test_has_return_registers()
    );
    trap_test_state = TRAP_TEST_IDLE;
    return passed;
}
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
_Noreturn void micros_trap_run_panic_test(void)
{
    micros_trap_panic_test_trigger();
    MICROS_PANIC(micros_trap_hart_id, "trap-test-returned");
}
#endif
