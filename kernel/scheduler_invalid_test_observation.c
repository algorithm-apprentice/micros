#include "kernel/scheduler_invalid_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/kernel_object_runtime.h"

bool micros_scheduler_invalid_test_before_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct scheduler_invalid_test_context *fixture =
        scheduler_invalid_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
    ) {
        return false;
    }
    scheduler_invalid_test_copy_bytes(
        &fixture->timer_objects_snapshot,
        objects,
        sizeof(fixture->timer_objects_snapshot)
    );
    scheduler_invalid_test_copy_bytes(
        &fixture->timer_frame_snapshot,
        frame,
        sizeof(fixture->timer_frame_snapshot)
    );
    fixture->timer_satp_snapshot = scheduler_invalid_test_read_satp();
    return true;
}

bool micros_scheduler_invalid_test_after_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct scheduler_invalid_test_context *fixture =
        scheduler_invalid_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || hart_handle.slot >= MICROS_HART_CAPACITY
    ) {
        return false;
    }
    scheduler_invalid_test_copy_bytes(
        &fixture->scheduler_snapshot_candidate,
        objects,
        sizeof(fixture->scheduler_snapshot_candidate)
    );
    fixture->scheduler_snapshot_candidate.harts[hart_handle.slot].timer =
        fixture->timer_objects_snapshot.harts[hart_handle.slot].timer;
    return (
        scheduler_invalid_test_bytes_equal(
            &fixture->scheduler_snapshot_candidate,
            &fixture->timer_objects_snapshot,
            sizeof(fixture->scheduler_snapshot_candidate)
        )
        && scheduler_invalid_test_bytes_equal(
            frame,
            &fixture->timer_frame_snapshot,
            sizeof(*frame)
        )
        && scheduler_invalid_test_read_satp() == fixture->timer_satp_snapshot
    );
}

bool micros_scheduler_invalid_test_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    uint64_t cause_code;

    if (
        hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return false;
    }
    cause_code = frame->scause & TEST_SCAUSE_CODE_MASK;
    if (user_timer) {
        return (
            (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
        );
    }
    if (
        (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
        || cause_code != TEST_EXCEPTION_USER_ECALL
        || frame->sepc
            != scheduler_invalid_test_user_address_of(micros_scheduler_payload_ecall)
    ) {
        return false;
    }
    frame->sepc += 4;
    return true;
}

bool micros_scheduler_invalid_test_report(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    bool outgoing
)
{
    struct scheduler_invalid_test_context *fixture =
        scheduler_invalid_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    const struct micros_process *process;
    bool expected_outgoing;

#ifdef MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    expected_outgoing = true;
#else
    expected_outgoing = false;
#endif
    if (objects != NULL && hart_handle.slot < MICROS_HART_CAPACITY) {
        scheduler_invalid_test_copy_bytes(
            &fixture->scheduler_snapshot_candidate,
            objects,
            sizeof(fixture->scheduler_snapshot_candidate)
        );
        fixture->scheduler_snapshot_candidate.harts[hart_handle.slot].timer =
            fixture->timer_objects_snapshot.harts[hart_handle.slot].timer;
    }
    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || outgoing != expected_outgoing
        || hart->current_thread.slot != fixture->threads[0].slot
        || hart->current_thread.generation
            != fixture->threads[0].generation
        || hart->ready_head[7].slot != fixture->threads[0].slot
        || hart->ready_head[7].generation
            != fixture->threads[0].generation
        || hart->ready_tail[7].slot != fixture->threads[1].slot
        || hart->ready_tail[7].generation
            != fixture->threads[1].generation
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || hart->trap.primary_stack_bottom
            != fixture->kernel_stack_bottom[0]
        || hart->trap.primary_stack_top != fixture->kernel_stack_top[0]
        || micros_process_resolve(
            objects,
            fixture->processes[0],
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || scheduler_invalid_test_read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        || !scheduler_invalid_test_bytes_equal(
            &fixture->scheduler_snapshot_candidate,
            &fixture->timer_objects_snapshot,
            sizeof(fixture->scheduler_snapshot_candidate)
        )
        || !scheduler_invalid_test_bytes_equal(
            frame,
            &fixture->timer_frame_snapshot,
            sizeof(*frame)
        )
    ) {
        return false;
    }
    uart_write("MICROS_SCHEDULER_INVALID_CONTEXT state=");
    uart_write(outgoing ? "outgoing" : "next");
    uart_write(" ownership=preserved accounting=kernel\n");
    uart_flush();
    return true;
}
