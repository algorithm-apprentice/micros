#include "kernel/elf_spawn_test_internal.h"

#include "micros/tty.h"

#define TARGET(profile) (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_##profile)

static struct elf_spawn_test_context scenario_context;

struct elf_spawn_test_context *elf_spawn_test_context_get(void)
{
    return &scenario_context;
}

static const struct micros_privilege_profile profiles[] = {
    {
        .id = MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .name = "BOOTSTRAP_LAUNCHER",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_REPLY,
        .kernel_operations = MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VM,
        .name = "VM",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL | MICROS_PRIVILEGE_OPERATION_REPLY,
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
        .kernel_operations = MICROS_KERNEL_OPERATION_VM_HANDOFF,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_PM,
        .name = "PM",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER) | TARGET(VM) | TARGET(VFS),
        .kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_TTY,
        .name = "TTY",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
        .notify_targets = TARGET(VFS),
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_RAMFS,
        .name = "RAMFS",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VFS,
        .name = "VFS",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER) | TARGET(TTY) | TARGET(RAMFS),
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_APPLICATION,
        .name = "APPLICATION",
        .operations = MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets = TARGET(PM) | TARGET(VFS),
    },
};

static const struct micros_bootstrap_expected_service expected_services[] = {
    {
        .service_id = MICROS_ELF_SPAWN_LAUNCHER_SERVICE_ID,
        .image_id = 101, .process_slot = 0,
        .profile_id = MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .service_name = "bootstrap-launcher", .profile_name = "BOOTSTRAP_LAUNCHER",
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
    },
    {
        .service_id = MICROS_ELF_SPAWN_VM_SERVICE_ID,
        .image_id = 102, .process_slot = 1,
        .profile_id = MICROS_PRIVILEGE_PROFILE_VM,
        .service_name = "vm", .profile_name = "VM",
        .prerequisites = UINT64_C(1),
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
        .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
    },
    {
        .service_id = MICROS_ELF_SPAWN_PM_SERVICE_ID,
        .image_id = 103, .process_slot = 2,
        .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
        .service_name = "pm", .profile_name = "PM",
        .prerequisites = UINT64_C(2),
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER) | TARGET(VM) | TARGET(VFS),
        .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
    },
    {
        .service_id = MICROS_ELF_SPAWN_TTY_SERVICE_ID,
        .image_id = 104, .process_slot = 3,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .service_name = "tty", .profile_name = "TTY",
        .prerequisites = UINT64_C(4),
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
        .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
        .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
    },
    {
        .service_id = MICROS_ELF_SPAWN_RAMFS_SERVICE_ID,
        .image_id = 105, .process_slot = 4,
        .profile_id = MICROS_PRIVILEGE_PROFILE_RAMFS,
        .service_name = "ramfs", .profile_name = "RAMFS",
        .prerequisites = UINT64_C(8),
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER),
    },
    {
        .service_id = MICROS_ELF_SPAWN_VFS_SERVICE_ID,
        .image_id = 106, .process_slot = 5,
        .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,
        .service_name = "vfs", .profile_name = "VFS",
        .prerequisites = UINT64_C(16),
        .call_targets = TARGET(BOOTSTRAP_LAUNCHER) | TARGET(TTY) | TARGET(RAMFS),
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {1, 4, true, 0, UINT64_C(1000000)},
    {2, 8, true, 0, UINT64_C(1000000)},
    {3, 8, true, 0, UINT64_C(1000000)},
    {4, 8, true, 0, UINT64_C(1000000)},
    {5, 8, true, 0, UINT64_C(1000000)},
    {6, 8, true, 0, UINT64_C(1000000)},
};

const struct micros_bootstrap_runtime_config micros_elf_spawn_test_config = {
    .manifest = &micros_elf_spawn_test_manifest,
    .expected_services = expected_services,
    .expected_service_count = MICROS_ELF_SPAWN_SERVICE_COUNT,
    .images = micros_elf_spawn_test_images,
    .image_count = MICROS_ELF_SPAWN_SERVICE_COUNT,
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = MICROS_ELF_SPAWN_SERVICE_COUNT,
    .scheduler_preemption_interval = UINT64_C(100000),
};
