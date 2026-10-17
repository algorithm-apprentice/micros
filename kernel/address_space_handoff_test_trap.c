#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_syscall.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

_Noreturn void micros_address_space_handoff_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
)
{
    micros_scheduler_test_enter_without_timer(
        (struct micros_thread_handle){
            .slot = (uint16_t)thread_slot,
            .generation = (uint32_t)thread_generation,
        }
    );
}

enum micros_address_space_handoff_test_trap_result
micros_address_space_handoff_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_address_space_handoff_test_context *fixture =
        micros_address_space_handoff_test_context();

    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor = handoff_test_current_actor(fixture, hart);
    struct micros_user_context expected;
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | (UINT64_C(1) << 6)
        | (UINT64_C(3) << 9)
        | (UINT64_C(3) << 13)
        | (UINT64_C(3) << 15)
        | (UINT64_C(1) << 19);

    if (fixture->test_state == HANDOFF_TEST_IDLE) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_INACTIVE;
    }
    if (
        fixture->test_state != HANDOFF_TEST_USER_RUNNING
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || (frame->scause >> 63) != 0
        || (frame->scause & (UINT64_C(1) << 63) - 1) != 8
        || actor >= HANDOFF_PROCESS_COUNT
        || frame->sepc
            != handoff_test_user_address_of(fixture,
                micros_address_space_handoff_payload_ecall
            )
        || frame->a0 != TEST_SYSCALL_CONTROL_MAGIC
        || frame->a7 != UINT64_MAX
        || fixture->syscall_control_action
            == HANDOFF_SYSCALL_CONTROL_NONE
    ) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
    }
    if (
        fixture->syscall_control_action
            == HANDOFF_SYSCALL_CONTROL_SWITCH
    ) {
        const struct micros_thread *next =
            &objects->threads[fixture->threads[fixture->syscall_control_next].slot];

        handoff_test_copy_bytes(fixture, &expected, &next->user_context, sizeof(expected));
        if (
            (
                next->scheduler_assigned
                ? micros_thread_runtime_flags_unset(
                    objects,
                    fixture->threads[fixture->syscall_control_next],
                    MICROS_THREAD_RTS_INACTIVE
                )
                : micros_thread_scheduler_admit(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    fixture->threads[fixture->syscall_control_next],
                    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                    UINT64_MAX,
                    true
                )
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                fixture->threads[actor]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
            || handoff_test_current_actor(fixture, hart)
                != fixture->syscall_control_next
            || !handoff_test_bytes_equal(fixture, frame, &expected, sizeof(expected))
            || !handoff_test_scheduler_state_valid(fixture,
                hart,
                fixture->syscall_control_next,
                true
            )
        ) {
            return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
        }
        fixture->syscall_control_action = HANDOFF_SYSCALL_CONTROL_NONE;
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_USER_RETURN;
    }
    if (
        fixture->syscall_control_action
            != HANDOFF_SYSCALL_CONTROL_FINISH
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_MISMATCH;
    }
    frame->sp = micros_address_space_handoff_test_saved_state[1];
    frame->sepc =
        (uintptr_t)micros_address_space_handoff_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    fixture->syscall_control_action = HANDOFF_SYSCALL_CONTROL_NONE;
    fixture->test_state = HANDOFF_TEST_SUPERVISOR;
    return MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_SUPERVISOR_RETURN;
}
