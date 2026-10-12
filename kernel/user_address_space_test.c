#include "micros/user_address_space.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/user_address_space_test_internal.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

extern const unsigned char micros_user_address_space_sum_fault[];
extern const unsigned char micros_user_address_space_sum_resume[];

enum micros_user_address_space_test_trap_result
micros_user_address_space_handle_test_trap(
    struct micros_trap_frame *frame
)
{
    struct micros_user_address_space_test_context *context
        = micros_user_address_space_test_context();
    uint64_t cause;

    if (context->sum_fault_state
        != MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_ARMED) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_INACTIVE;
    }
    if (frame == NULL || (frame->scause & MICROS_SCAUSE_INTERRUPT) != 0
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) != 0
        || frame->hart_context == 0) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & MICROS_SCAUSE_CODE_MASK;
    if (cause != MICROS_USER_ADDRESS_SPACE_TEST_LOAD_PAGE_FAULT
        || frame->sepc != (uintptr_t)micros_user_address_space_sum_fault
        || frame->stval != context->sum_fault_virtual_address) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH;
    }
    context->sum_fault_state
        = MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_HANDLED;
    frame->sepc = (uintptr_t)micros_user_address_space_sum_resume;
    return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_HANDLED;
}

static bool run_ordered_scenarios(
    struct micros_user_address_space_test_context *context
)
{
    return (
        micros_user_address_space_test_create_roots(context)
        && micros_user_address_space_test_validate_kernel_roots(context)
        && micros_user_address_space_test_mapping_rejections(context)
        && micros_user_address_space_test_last_page_lifecycle(context)
        && micros_user_address_space_test_mapping_operations(context)
        && micros_user_address_space_test_reuse_page(context)
        && micros_user_address_space_test_reject_corruption(context)
        && micros_user_address_space_test_destroy_and_reuse(context)
        && micros_user_address_space_test_enter_handoff(context)
        && micros_user_address_space_test_post_handoff_revocation(
            context
        )
        && micros_user_address_space_test_finish_lifecycle(context)
    );
}

bool micros_user_address_space_runtime_run_self_test(void)
{
    struct micros_user_address_space_test_context *context
        = micros_user_address_space_test_context();
    uintptr_t saved_status;
    bool passed;

    saved_status = riscv_irq_save();
    passed
        = (micros_user_address_space_test_acquire(context)
           && run_ordered_scenarios(context));
    micros_user_address_space_test_reset_fault(context);
    riscv_irq_restore(saved_status);
    return passed;
}
