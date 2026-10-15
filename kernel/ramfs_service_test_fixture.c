#include "kernel/ramfs_service_test_internal.h"

#include "kernel/ramfs_service_test_fixture.h"

static const struct micros_privilege_profile profiles[] = {
    {
        .id =
            MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
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
};

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id = MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id =
                MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .call_targets = 0,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id = MICROS_RAMFS_TEST_VM_SERVICE_ID,
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
            .service_id = MICROS_RAMFS_TEST_PM_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
            .service_name = "pm",
            .profile_name = "PM",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_RAMFS_TEST_VM_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
                | UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        },
        {
            .service_id = MICROS_RAMFS_TEST_TTY_SERVICE_ID,
            .image_id = 104,
            .process_slot = 3,
            .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
            .service_name = "tty",
            .profile_name = "TTY",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_RAMFS_TEST_PM_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
            .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
            .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
            .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        },
        {
            .service_id = MICROS_RAMFS_TEST_RAMFS_SERVICE_ID,
            .image_id = 105,
            .process_slot = 4,
            .profile_id = MICROS_PRIVILEGE_PROFILE_RAMFS,
            .service_name = "ramfs",
            .profile_name = "RAMFS",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_RAMFS_TEST_TTY_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        },
        {
            .service_id = MICROS_RAMFS_TEST_VFS_SERVICE_ID,
            .image_id = 106,
            .process_slot = 5,
            .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,
            .service_name = "ramfs-test-vfs",
            .profile_name = "VFS",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_RAMFS_TEST_RAMFS_SERVICE_ID - 1),
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
    };

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id = MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_RAMFS_TEST_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_RAMFS_TEST_PM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_RAMFS_TEST_TTY_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_RAMFS_TEST_RAMFS_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_RAMFS_TEST_VFS_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

const struct micros_bootstrap_runtime_config ramfs_test_runtime_config = {
    .manifest = &micros_ramfs_service_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_ramfs_service_test_images,
    .image_count = MICROS_RAMFS_TEST_SERVICE_COUNT,
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval = UINT64_C(100000),
};
