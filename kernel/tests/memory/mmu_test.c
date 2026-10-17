#include "micros/kernel_address_space.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MMU_TEST_EXCEPTION_INSTRUCTION_PAGE_FAULT = 12,
    MMU_TEST_EXCEPTION_STORE_PAGE_FAULT = 15,
};

enum mmu_test_state {
    MMU_TEST_IDLE,
    MMU_TEST_STORE_ARMED,
    MMU_TEST_STORE_HANDLED,
    MMU_TEST_EXECUTE_ARMED,
    MMU_TEST_EXECUTE_HANDLED,
};

void micros_mmu_store_test_trigger(void);
void micros_mmu_execute_test_trigger(void);

extern const unsigned char micros_mmu_text_probe[];
extern const unsigned char micros_mmu_store_fault[];
extern const unsigned char micros_mmu_store_resume[];
extern const unsigned char micros_mmu_writable_probe[];
extern const unsigned char micros_mmu_execute_resume[];

static volatile enum mmu_test_state test_state;
static volatile uint32_t test_trap_count;

static bool frame_has_common_expected_state(
    const struct micros_trap_frame *frame
)
{
    return (
        frame != NULL
        && (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SIE) == 0
        && frame->hart_context != 0
    );
}

enum micros_mmu_test_trap_result
micros_kernel_address_space_handle_test_trap(
    struct micros_trap_frame *frame
)
{
    uint64_t cause;

    if (
        test_state != MMU_TEST_STORE_ARMED
        && test_state != MMU_TEST_EXECUTE_ARMED
    ) {
        return MICROS_MMU_TEST_TRAP_INACTIVE;
    }
    if (!frame_has_common_expected_state(frame)) {
        return MICROS_MMU_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & MICROS_SCAUSE_CODE_MASK;

    if (test_state == MMU_TEST_STORE_ARMED) {
        if (
            cause != MMU_TEST_EXCEPTION_STORE_PAGE_FAULT
            || frame->sepc != (uintptr_t)micros_mmu_store_fault
            || frame->stval != (uintptr_t)micros_mmu_text_probe
        ) {
            return MICROS_MMU_TEST_TRAP_MISMATCH;
        }
        ++test_trap_count;
        test_state = MMU_TEST_STORE_HANDLED;
        frame->sepc = (uintptr_t)micros_mmu_store_resume;
        return MICROS_MMU_TEST_TRAP_HANDLED;
    }

    if (
        cause != MMU_TEST_EXCEPTION_INSTRUCTION_PAGE_FAULT
        || frame->sepc != (uintptr_t)micros_mmu_writable_probe
        || frame->stval != (uintptr_t)micros_mmu_writable_probe
    ) {
        return MICROS_MMU_TEST_TRAP_MISMATCH;
    }
    ++test_trap_count;
    test_state = MMU_TEST_EXECUTE_HANDLED;
    frame->sepc = (uintptr_t)micros_mmu_execute_resume;
    return MICROS_MMU_TEST_TRAP_HANDLED;
}

bool micros_kernel_address_space_run_self_test(void)
{
    test_trap_count = 0;
    test_state = MMU_TEST_STORE_ARMED;
    micros_mmu_store_test_trigger();
    if (
        test_state != MMU_TEST_STORE_HANDLED
        || test_trap_count != 1
    ) {
        test_state = MMU_TEST_IDLE;
        return false;
    }

    test_state = MMU_TEST_EXECUTE_ARMED;
    micros_mmu_execute_test_trigger();
    if (
        test_state != MMU_TEST_EXECUTE_HANDLED
        || test_trap_count != 2
    ) {
        test_state = MMU_TEST_IDLE;
        return false;
    }
    test_state = MMU_TEST_IDLE;
    return true;
}
