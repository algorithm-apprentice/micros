#include "ipc_ecall_test_internal.h"

_Noreturn void micros_ipc_ecall_core_runtime_run_self_test(void)
{
    struct ipc_ecall_test_context *fixture = ipc_ecall_test_context_get();
    struct ipc_ecall_setup_state state;
    uintptr_t saved_status = riscv_irq_save();

    if (!ipc_ecall_test_prepare_environment(fixture, &state)) {
        ipc_ecall_test_setup_failure(saved_status);
    }
    __asm__ volatile("fence.i" : : : "memory");
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_ipc_ecall_test_saved_sp),
          "=r"(micros_ipc_ecall_test_saved_gp),
          "=r"(micros_ipc_ecall_test_saved_tp)
    );
    (void)saved_status;
    micros_scheduler_test_enter_without_timer(fixture->client_thread);

    ipc_ecall_test_setup_failure(saved_status);
}
