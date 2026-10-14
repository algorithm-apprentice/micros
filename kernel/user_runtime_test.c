#include "kernel/user_runtime_test_internal.h"

#include "arch/riscv64/interrupt.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"

static const uint64_t USER_RUNTIME_TIMER_INTERVAL =
    UINT64_C(0x000000003b9aca00);

uintptr_t micros_user_runtime_test_saved_sp;
uintptr_t micros_user_runtime_test_saved_gp;
uintptr_t micros_user_runtime_test_saved_tp;

_Noreturn void micros_user_runtime_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "RUNTIME_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 2,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "RUNTIME_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .send_targets =
                (UINT32_C(1) << 1)
                | (UINT32_C(1) << 3),
        },
        {
            .id = 3,
            .name = "RUNTIME_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 2,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    uintptr_t saved_status = riscv_irq_save();
    struct user_runtime_test_context *runtime = user_runtime_test_state();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (!user_runtime_test_preflight(runtime, ledger, objects)) {
        goto failure;
    }
    if (
        !user_runtime_test_initialize_mechanisms(
            runtime,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        )
    ) {
        goto failure;
    }
    if (!user_runtime_test_prepare_identities(runtime, objects)) {
        goto failure;
    }
    if (!user_runtime_test_prepare_images(runtime, objects)) {
        goto failure;
    }
    if (
        !user_runtime_test_admit_threads(
            runtime,
            objects,
            USER_RUNTIME_TIMER_INTERVAL
        )
    ) {
        goto failure;
    }
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_user_runtime_test_saved_sp),
          "=r"(micros_user_runtime_test_saved_gp),
          "=r"(micros_user_runtime_test_saved_tp)
    );
    (void)saved_status;
    (void)micros_scheduler_start();

failure:
    riscv_irq_restore(saved_status);
    user_runtime_test_report_setup_failure();
}
