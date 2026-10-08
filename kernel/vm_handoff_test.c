#include "kernel/vm_handoff_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "kernel/vm_handoff_test_fixture.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "tests/qemu/vm_handoff_protocol.h"

#define MICROS_TEST_EXCEPTION_BREAKPOINT UINT64_C(3)

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id =
                MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id = 1,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id = MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID,
            .image_id = 102,
            .process_slot = 1,
            .profile_id = 2,
            .service_name = "vm",
            .profile_name = "VM",
            .prerequisites = UINT64_C(1),
            .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
        },
        {
            .service_id = MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = 3,
            .service_name = "vm-handoff-probe",
            .profile_name = "VM_HANDOFF_PROBE",
            .prerequisites = UINT64_C(1) << 1,
        },
    };

static const struct micros_privilege_profile profiles[] = {
    {
        .id = 1,
        .name = "BOOTSTRAP_LAUNCHER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL,
    },
    {
        .id = 2,
        .name = "VM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .call_targets = UINT32_C(1) << 1,
        .kernel_operations = MICROS_KERNEL_OPERATION_VM_HANDOFF,
    },
    {
        .id = 3,
        .name = "VM_HANDOFF_PROBE",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (UINT32_C(1) << 1) | (UINT32_C(1) << 2),
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id = MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

static const struct micros_bootstrap_runtime_config runtime_config = {
    .manifest = &micros_vm_handoff_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_vm_handoff_test_images,
    .image_count =
        sizeof(micros_vm_handoff_test_images)
            / sizeof(micros_vm_handoff_test_images[0]),
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval =
        MICROS_VM_HANDOFF_TEST_SCHEDULER_INTERVAL,
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

_Noreturn void micros_vm_handoff_test_launch(void)
{
    micros_bootstrap_runtime_launch(&runtime_config);
}

_Noreturn void micros_vm_handoff_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_grant_registry *grants =
        micros_grant_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *probe;
    struct micros_frame_owner temporary_owner;
    struct micros_thread_handle current;
    uint64_t unchanged = UINT64_C(0x1122334455667788);
    size_t index;

    if (
        hart == NULL
        || frame == NULL
        || bootstrap == NULL
        || handoff == NULL
        || ownership == NULL
        || registry == NULL
        || grants == NULL
        || objects == NULL
        || frame->scause != MICROS_TEST_EXCEPTION_BREAKPOINT
        || frame->sepc != micros_vm_handoff_test_report_address
        || frame->a0 != MICROS_VM_HANDOFF_TEST_REPORT_MAGIC
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || handoff->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || grants->active_count != 0
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
            &temporary_owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(
            temporary_owner,
            &unchanged
        ) != MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        || unchanged != UINT64_C(0x1122334455667788)
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        test_failure(hart, frame, "vm-handoff-test-state");
    }
    probe = micros_bootstrap_control_find_binding(
        bootstrap,
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    if (
        probe == NULL
        || current.slot != probe->thread.slot
        || current.generation != probe->thread.generation
    ) {
        test_failure(hart, frame, "vm-handoff-test-current");
    }
    for (index = 0; index < bootstrap->entry_count; ++index) {
        if (
            bootstrap->transitions.entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            test_failure(hart, frame, "vm-handoff-test-readiness");
        }
    }
    uart_write(
        "MICROS_VM_HANDOFF_TEST_PASS "
        "snapshot=validated ownership=handed-off vm=wired "
        "readiness=acknowledged authority=vm\n"
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
