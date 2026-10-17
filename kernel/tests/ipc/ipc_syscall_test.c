#include "kernel/tests/ipc/ipc_syscall_test_internal.h"

_Noreturn void micros_ipc_syscall_runtime_run_self_test(void)
{
    struct ipc_syscall_test_context *fixture = ipc_syscall_test_context_get();
    struct ipc_syscall_setup_state state;
    uintptr_t saved_status = riscv_irq_save();

    if (!ipc_syscall_test_prepare_environment(fixture, &state)) {
        ipc_syscall_test_setup_failure(saved_status);
    }
    __asm__ volatile("fence.i" : : : "memory");
    if (
        micros_scheduler_initialize(IPC_SYSCALL_TEST_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[IPC_SYSCALL_CLIENT],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[IPC_SYSCALL_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        ipc_syscall_test_setup_failure(saved_status);
    }
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_ipc_syscall_test_saved_sp),
          "=r"(micros_ipc_syscall_test_saved_gp),
          "=r"(micros_ipc_syscall_test_saved_tp)
    );
    (void)saved_status;
    (void)micros_scheduler_start();

    ipc_syscall_test_setup_failure(saved_status);
}
