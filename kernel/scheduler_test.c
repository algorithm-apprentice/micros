#include "kernel/scheduler_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"

volatile uint64_t micros_scheduler_test_idle_bypass_once;
volatile uint64_t micros_scheduler_test_idle_bypass_observed;

_Noreturn void micros_scheduler_runtime_run_self_test(void)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "SCHEDULER_TEST",
            .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 1,
            .notify_targets = UINT32_C(1) << 1,
        },
    };
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (ledger == NULL || objects == NULL) {
        goto failure;
    }
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    fixture->registry = micros_ipc_runtime_authoritative_registry();
    if (fixture->registry == NULL) {
        goto failure;
    }
    fixture->baseline_owned = ledger->owned_frame_count;
    fixture->baseline_free = ledger->allocator->free_frame_count;
    payload_size =
        (uintptr_t)micros_scheduler_payload_end
        - (uintptr_t)micros_scheduler_payload_start;
    if (payload_size == 0 || payload_size > MICROS_SV39_PAGE_SIZE) {
        goto failure;
    }
    for (index = 0; index < 2; ++index) {
        if (!scheduler_test_reserve_process(
            fixture, objects, index, code_permissions, data_permissions
        )) {
            goto failure;
        }
        if (!scheduler_test_prepare_process_image(fixture, index, payload_size)) {
            goto failure;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");
    if (!scheduler_test_admit_threads(fixture)) {
        goto failure;
    }
    if (!scheduler_test_prove_malformed_completion(fixture, objects)) {
        goto failure;
    }
    if (!scheduler_test_stage_start_completion(fixture, objects)) {
        goto failure;
    }
    if (!scheduler_test_prove_start_program_failure(fixture, objects)) {
        goto failure;
    }

    (void)saved_status;
    micros_scheduler_test_enter();
    scheduler_test_finish_test(fixture);

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE scheduler-setup\n");
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
