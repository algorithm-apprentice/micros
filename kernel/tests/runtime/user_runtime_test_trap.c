#include "kernel/tests/runtime/user_runtime_test_internal.h"

#include "arch/riscv64/trap_context.h"
#include "kernel/tests/runtime/user_runtime_image.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"

static bool user_runtime_test_hold_other_threads(
    const struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    size_t current
)
{
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        const struct micros_thread *thread;

        if (actor == current) {
            continue;
        }
        if (
            micros_thread_resolve(
                objects,
                runtime->threads[actor],
                &thread
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        if (
            (
                thread->runtime_flags
                & MICROS_THREAD_RTS_INACTIVE
            ) == 0
            && micros_thread_runtime_flags_set(
                objects,
                runtime->threads[actor],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

bool micros_user_runtime_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct user_runtime_test_context *runtime = user_runtime_test_state();
    struct micros_user_runtime_test_config config;
    size_t actor = user_runtime_test_current_actor(runtime, hart);

    runtime->final_client_send = false;
    if (
        frame == NULL
        || actor >= USER_RUNTIME_PROCESS_COUNT
    ) {
        return false;
    }
    if (
        actor == USER_RUNTIME_CLIENT
        && frame->a7 == MICROS_SYSCALL_ABI_SEND
        && user_runtime_test_read_configuration(runtime, actor, &config)
        && (
            config.status
            & MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE
        ) != 0
    ) {
        runtime->final_client_send = true;
    }
    return true;
}

bool micros_user_runtime_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    struct user_runtime_test_context *runtime = user_runtime_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    (void)hart;
    (void)frame;
    if (!runtime->final_client_send) {
        return true;
    }
    runtime->final_client_send = false;
    return (
        objects != NULL
        && syscall_return == MICROS_SYSCALL_RETURN_CAPTURED
        && micros_thread_install_policy(
            objects,
            runtime->threads[USER_RUNTIME_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER + 1,
            UINT64_MAX
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

enum micros_user_runtime_test_trap_result
micros_user_runtime_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct user_runtime_test_context *runtime = user_runtime_test_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const uint64_t control_mask =
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
    uint64_t cause;
    size_t actor = user_runtime_test_current_actor(runtime, hart);

    if (
        objects == NULL
        || frame == NULL
        || actor >= USER_RUNTIME_PROCESS_COUNT
        || (frame->scause & (UINT64_C(1) << 63)) != 0
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & ((UINT64_C(1) << 63) - 1);
    if (
        cause == USER_RUNTIME_EXCEPTION_STORE_PAGE_FAULT
        && actor == USER_RUNTIME_SERVER
        && !runtime->rodata_fault_seen
        && frame->sepc
            == micros_user_runtime_test_image.rodata_fault_address
        && frame->stval
            == micros_user_runtime_test_image.rodata_address
        && user_runtime_test_writable_pages_match_snapshot(runtime, actor)
    ) {
        frame->sepc =
            micros_user_runtime_test_image.rodata_resume_address;
        runtime->rodata_fault_seen = true;
        return MICROS_USER_RUNTIME_TEST_TRAP_USER_RETURN;
    }
    if (
        cause != USER_RUNTIME_EXCEPTION_BREAKPOINT
        || frame->sepc
            != micros_user_runtime_test_image.service_returned_address
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    runtime->returned_actor = actor;
    if (
        !user_runtime_test_hold_other_threads(runtime, objects, actor)
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    frame->sp = micros_user_runtime_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_user_runtime_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return MICROS_USER_RUNTIME_TEST_TRAP_SUPERVISOR_RETURN;
}
