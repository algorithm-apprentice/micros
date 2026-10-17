#include "kernel/tests/vm/vm_scratch_preemption_test_internal.h"

static struct vm_scratch_preemption_test_context test_context;

struct vm_scratch_preemption_test_context *vm_scratch_test_context_get(void)
{
    return &test_context;
}

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id =
                MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID,
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
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
            .service_name = "pm",
            .profile_name = "PM",
            .prerequisites =
                UINT64_C(1)
                << (
                    MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
                    - 1
                ),
            .call_targets =
                UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
                | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        },
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID,
            .image_id = 104,
            .process_slot = 3,
            .profile_id =
                MICROS_VM_SCRATCH_PREEMPTION_DRIVER_PROFILE_ID,
            .service_name = "vm-scratch-driver",
            .profile_name = "VM_SCRATCH_DRIVER",
            .prerequisites =
                UINT64_C(1)
                << (
                    MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
                    - 1
                ),
            .call_targets =
                (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
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
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
            | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
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
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
            ),
    },
    {
        .id = MICROS_VM_SCRATCH_PREEMPTION_DRIVER_PROFILE_ID,
        .name = "VM_SCRATCH_DRIVER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
            ),
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

const struct micros_bootstrap_runtime_config vm_scratch_test_runtime_config = {
    .manifest = &micros_vm_scratch_preemption_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_vm_scratch_preemption_test_images,
    .image_count =
        sizeof(micros_vm_scratch_preemption_test_images)
            / sizeof(micros_vm_scratch_preemption_test_images[0]),
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval =
        MICROS_VM_SCRATCH_PREEMPTION_SCHEDULER_INTERVAL,
};
