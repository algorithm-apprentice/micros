#include "kernel/elf_spawn_test_internal.h"

#include "servers/pm/pm_service_internal.h"
#include "tests/qemu/vm_server_internal.h"
#include "micros/sv39.h"

static bool elf_spawn_read_object(
    enum micros_elf_spawn_service_object_index index,
    size_t offset, void *destination, size_t size
)
{
    const struct micros_elf_spawn_service_object *object = &micros_elf_spawn_test_objects[index];
    const struct micros_bootstrap_control_state *bootstrap = micros_bootstrap_runtime_state();
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(bootstrap, object->service_id);

    return (
        binding != NULL && object->reserved == 0
        && offset <= object->size && size <= object->size - offset
        && elf_spawn_test_read_process(
            binding->process, object->address + offset, destination, size,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE
        )
    );
}

static bool elf_spawn_read_vm_boot_info(struct micros_vm_boot_info *info)
{
    const struct micros_bootstrap_control_state *bootstrap = micros_bootstrap_runtime_state();
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(bootstrap, MICROS_ELF_SPAWN_VM_SERVICE_ID);

    return binding != NULL && elf_spawn_test_read_process(
        binding->process, micros_elf_spawn_test_images[1].vm_boot_info_address,
        info, sizeof(*info), MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE
    );
}

bool elf_spawn_test_capture_services(struct micros_elf_spawn_service_resources *snapshot)
{
    return (
        elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_PM_CONTEXT,
            offsetof(struct pm_service_context, process_table), &snapshot->pm, sizeof(snapshot->pm))
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_PM_CONTEXT,
            offsetof(struct pm_service_context, coordinator), &snapshot->coordinator,
            sizeof(snapshot->coordinator))
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_VM_CONTEXT,
            offsetof(struct vm_server_context, vm_state), &snapshot->vm, sizeof(snapshot->vm))
        && elf_spawn_read_vm_boot_info(&snapshot->boot_info)
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_VFS_STATE, 0,
            &snapshot->vfs, sizeof(snapshot->vfs))
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_RAMFS_STATE, 0,
            &snapshot->ramfs, sizeof(snapshot->ramfs))
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_TTY_STATE, 0,
            &snapshot->tty, sizeof(snapshot->tty))
        && elf_spawn_read_object(MICROS_ELF_SPAWN_OBJECT_TTY_UART, 0,
            &snapshot->uart, sizeof(snapshot->uart))
    );
}
