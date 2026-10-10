#include "kernel/pm_service_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/pm_service_test_fixture.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "tests/qemu/pm_service_protocol.h"

#define MICROS_TEST_EXCEPTION_BREAKPOINT UINT64_C(3)

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id = MICROS_PM_TEST_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id =
                MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id = MICROS_PM_TEST_VM_SERVICE_ID,
            .image_id = 102,
            .process_slot = 1,
            .profile_id = MICROS_PRIVILEGE_PROFILE_VM,
            .service_name = "vm",
            .profile_name = "VM",
            .prerequisites = UINT64_C(1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
        },
        {
            .service_id = MICROS_PM_TEST_PM_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
            .service_name = "pm",
            .profile_name = "PM",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_PM_TEST_VM_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        },
        {
            .service_id = MICROS_PM_TEST_PROBE_SERVICE_ID,
            .image_id = 104,
            .process_slot = 3,
            .profile_id = MICROS_PM_TEST_PROBE_PROFILE_ID,
            .service_name = "pm-test-probe",
            .profile_name = "PM_TEST_PROBE",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_PM_TEST_PM_SERVICE_ID - 1),
            .call_targets =
                (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                ) | (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_PM
                ),
        },
    };

static const struct micros_privilege_profile profiles[] = {
    {
        .id = MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .name = "BOOTSTRAP_LAUNCHER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VM,
        .name = "VM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .kernel_operations = MICROS_KERNEL_OPERATION_VM_HANDOFF,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_PM,
        .name = "PM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_TTY,
        .name = "TTY",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .notify_targets =
            UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_RAMFS,
        .name = "RAMFS",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VFS,
        .name = "VFS",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS
            ),
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_APPLICATION,
        .name = "APPLICATION",
        .operations = MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_PM
            ) | (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_VFS
            ),
    },
    {
        .id = MICROS_PM_TEST_PROBE_PROFILE_ID,
        .name = "PM_TEST_PROBE",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_PM
            ),
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id = MICROS_PM_TEST_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_PM_TEST_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_PM_TEST_PM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_PM_TEST_PROBE_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

static const struct micros_bootstrap_runtime_config runtime_config = {
    .manifest = &micros_pm_service_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_pm_service_test_images,
    .image_count =
        sizeof(micros_pm_service_test_images)
            / sizeof(micros_pm_service_test_images[0]),
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval =
        MICROS_PM_TEST_SCHEDULER_INTERVAL,
};

static _Noreturn void test_failure(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static bool process_has_no_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->owner.slot == process.slot
            && thread->owner.generation == process.generation
        ) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_pm_service_test_launch(void)
{
    micros_bootstrap_runtime_launch(&runtime_config);
}

_Noreturn void micros_pm_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_pm_control_state *control =
        micros_pm_control_runtime_state();
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_grant_registry *grants =
        micros_grant_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *pm;
    struct micros_process_handle consumed = {
        .slot = MICROS_PM_TEST_RESERVED_PROCESS_SLOT,
        .generation = 1,
    };
    const struct micros_process *stale = NULL;
    struct micros_thread_handle current;
    uint64_t frame_count = UINT64_MAX;
    uint32_t next_generation = 0;
    size_t index;

    if (
        hart == NULL
        || frame == NULL
        || bootstrap == NULL
        || handoff == NULL
        || control == NULL
        || ownership == NULL
        || registry == NULL
        || grants == NULL
        || objects == NULL
        || frame->scause != MICROS_TEST_EXCEPTION_BREAKPOINT
        || frame->sepc != micros_pm_service_test_report_address
        || frame->a0 != MICROS_PM_TEST_REPORT_MAGIC
        || frame->a1 != UINT64_C(1)
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || handoff->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || control->last_transaction != UINT64_C(1)
        || control->active_transaction != 0
        || control->reserved_process.slot != 0
        || control->reserved_process.generation != 0
        || objects->live_process_count != MICROS_PM_TEST_SERVICE_COUNT
        || objects->live_thread_count != MICROS_PM_TEST_SERVICE_COUNT
        || objects->processes[consumed.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        || objects->processes[consumed.slot].generation
            != consumed.generation
        || objects->processes[consumed.slot].live_thread_count != 0
        || objects->processes[consumed.slot].address_space_root != 0
        || objects->processes[consumed.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || objects->processes[consumed.slot].privilege_profile != 0
        || objects->processes[consumed.slot]
            .endpoint_lifecycle_consumed
        || registry->endpoints[consumed.slot].state
            != MICROS_ENDPOINT_STATE_FREE
        || !process_has_no_thread(objects, consumed)
        || grants->active_count != 0
        || micros_frame_ownership_count_process(
            ownership,
            consumed,
            &frame_count
        ) != MICROS_FRAME_OWNERSHIP_OK
        || frame_count != 0
        || micros_process_resolve(objects, consumed, &stale)
            != MICROS_KERNEL_OBJECT_ERROR_STALE
        || stale != NULL
        || micros_process_next_generation(
            consumed.slot,
            consumed.generation,
            &next_generation
        ) != MICROS_KERNEL_OBJECT_OK
        || next_generation != 2
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_PM_CONTROL_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        test_failure(hart, frame, "pm-service-test-state");
    }
    pm = micros_bootstrap_control_find_binding(
        bootstrap,
        MICROS_PM_TEST_PM_SERVICE_ID
    );
    if (
        pm == NULL
        || current.slot != pm->thread.slot
        || current.generation != pm->thread.generation
    ) {
        test_failure(hart, frame, "pm-service-test-current");
    }
    for (index = 0; index < bootstrap->entry_count; ++index) {
        if (
            bootstrap->transitions.entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            test_failure(hart, frame, "pm-service-test-readiness");
        }
    }
    uart_write(
        "MICROS_PM_SERVICE_TEST_PASS "
        "handoff=complete readiness=acknowledged protocol=stable "
        "sealed=received reserve=aborted resources=clean "
        "generation=advanced transaction=advanced\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
