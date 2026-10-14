#include "kernel/scheduler_invalid_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"

_Noreturn void micros_scheduler_invalid_runtime_run_self_test(void)
{
    struct scheduler_invalid_test_context *fixture =
        scheduler_invalid_test_state();
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t stack_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    payload_size =
        (uintptr_t)micros_scheduler_payload_end
        - (uintptr_t)micros_scheduler_payload_start;
    if (!scheduler_invalid_test_preflight_payload(objects, payload_size)) {
        goto failure;
    }
    for (index = 0; index < 2; ++index) {
        if (!scheduler_invalid_test_reserve_process(
            fixture, objects, index, code_permissions, stack_permissions
        )) {
            goto failure;
        }
        if (!scheduler_invalid_test_prepare_image(fixture, index, payload_size)) {
            goto failure;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");
    if (!scheduler_invalid_test_admit_threads(fixture)) {
        goto failure;
    }
#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
    scheduler_invalid_test_corrupt_next_context(fixture, objects);
#endif

    (void)saved_status;
    micros_scheduler_test_enter();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE scheduler-invalid-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void micros_scheduler_test_start_production(void)
{
    enum micros_scheduler_error error = micros_scheduler_start();

    uart_write("MICROS_TEST_FAILURE scheduler-start-returned error=");
    uart_write_hex64((uint64_t)error);
    uart_write("\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
