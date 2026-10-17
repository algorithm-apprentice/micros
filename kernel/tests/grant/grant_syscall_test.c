#include "kernel/tests/grant/grant_syscall_test_internal.h"

uintptr_t micros_grant_syscall_test_saved_sp;
uintptr_t micros_grant_syscall_test_saved_gp;
uintptr_t micros_grant_syscall_test_saved_tp;

static bool grant_syscall_test_prepare_actors(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t payload_size,
    size_t *actor
)
{
    for (
        *actor = 0;
        *actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++*actor
    ) {
        if (
            micros_process_create(objects, &fixture->processes[*actor])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                fixture->endpoint_registry,
                objects,
                fixture->processes[*actor],
                &fixture->endpoints[*actor]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                fixture->endpoint_registry,
                objects,
                fixture->processes[*actor],
                (uint8_t)(*actor + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                fixture->endpoint_registry,
                objects,
                fixture->endpoints[*actor]
            ) != MICROS_ENDPOINT_OK
            || !grant_syscall_test_prepare_process(
                fixture,
                objects,
                *actor,
                micros_grant_syscall_payload_start,
                payload_size
            )
        ) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_grant_syscall_runtime_run_self_test(void)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "GRANT_SYSCALL_GRANTOR",
            .operations = MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "GRANT_SYSCALL_GRANTEE",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 3,
            .name = "GRANT_SYSCALL_WRONG",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t actor;
    uintptr_t saved_status = riscv_irq_save();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (ledger == NULL || objects == NULL) {
        goto failure;
    }
    fixture->baseline_owned = ledger->owned_frame_count;
    fixture->baseline_free = ledger->allocator->free_frame_count;
    fixture->baseline_processes = objects->live_process_count;
    fixture->baseline_threads = objects->live_thread_count;
    fixture->baseline_harts = objects->registered_hart_count;
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            fixture->endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            fixture->grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        goto failure;
    }
    payload_size =
        (uintptr_t)micros_grant_syscall_payload_end
        - (uintptr_t)micros_grant_syscall_payload_start;
    if (
        payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        goto failure;
    }
    if (!grant_syscall_test_prepare_actors(fixture, objects, payload_size, &actor)) {
        goto failure;
    }
    if (
        !grant_syscall_test_arm_command(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            &objects->threads[
                fixture->threads[GRANT_SYSCALL_GRANTOR].slot
            ].user_context,
            GRANT_COMMAND_IPC_NOTIFY
        )
    ) {
        goto failure;
    }
    if (
        micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    __asm__ volatile("fence.i" : : : "memory");
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_grant_syscall_test_saved_sp),
          "=r"(micros_grant_syscall_test_saved_gp),
          "=r"(micros_grant_syscall_test_saved_tp)
    );
    (void)saved_status;
    micros_scheduler_test_enter_without_timer(
        fixture->threads[GRANT_SYSCALL_GRANTOR]
    );

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE grant-syscall-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static bool grant_syscall_test_close_all_endpoints(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t *actor,
    uint64_t *failure_stage
)
{
    for (
        *actor = 0;
        *actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++*actor
    ) {
        if (!grant_syscall_test_close_endpoint(fixture, objects, *actor)) {
            *failure_stage = UINT64_C(0x10) + *actor;
            return false;
        }
    }
    return true;
}

static bool grant_syscall_test_release_all_resources(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t *actor,
    uint64_t *failure_stage
)
{
    for (
        *actor = 0;
        *actor < GRANT_SYSCALL_PROCESS_COUNT;
        ++*actor
    ) {
        if (
            !grant_syscall_test_release_thread(fixture, objects, *actor)
            || !grant_syscall_test_release_address_space(fixture, objects, *actor)
            || micros_frame_ownership_runtime_release_process(
                objects,
                fixture->processes[*actor]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            *failure_stage = UINT64_C(0x20) + *actor;
            return false;
        }
    }
    return true;
}

_Noreturn void micros_grant_syscall_test_finish(void)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *kernel_space =
        micros_kernel_address_space_report();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || kernel_space == NULL
        || objects == NULL
        || fixture->endpoint_registry == NULL
        || fixture->grant_registry == NULL
    ) {
        goto failure;
    }
    if (!grant_syscall_test_close_all_endpoints(fixture, objects, &actor, &failure_stage)) {
        goto failure;
    }
    failure_stage = 2;
    if (!grant_syscall_test_release_all_resources(fixture, objects, &actor, &failure_stage)) {
        goto failure;
    }
    failure_stage = 3;
    if (
        fixture->grant_registry->active_count != 0
        || !grant_syscall_test_endpoint_registry_is_empty(fixture)
        || objects->live_process_count != fixture->baseline_processes
        || objects->live_thread_count != fixture->baseline_threads
        || objects->registered_hart_count != fixture->baseline_harts
        || ledger->owned_frame_count != fixture->baseline_owned
        || ledger->allocator->free_frame_count != fixture->baseline_free
        || !grant_syscall_test_scheduler_is_at_idle_baseline(objects)
        || grant_syscall_test_read_satp()
            != grant_syscall_test_expected_satp(kernel_space->root_physical_address)
    ) {
        goto failure;
    }
    failure_stage = 4;
    if (
        micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_GRANT_SYSCALL_TEST_PASS "
        "namespace=unified phase=bootstrap lifecycle=checked "
        "directions=checked errors=stable "
        "registers=preserved cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE grant-syscall-finish=");
    uart_write_hex64(failure_stage);
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
