#include "servers/pm/pm_coordinator_internal.h"

enum pm_coordinator_step pm_coordinator_classify_vfs_result(
    struct pm_coordinator_spawn *spawn, uint32_t command, int32_t result
)
{
    if (result == MICROS_VFS_CONTROL_RESULT_OK) {
        return PM_COORDINATOR_ADVANCED;
    }
    if (
        result == MICROS_VFS_CONTROL_RESULT_NO_SPACE
        && (command == MICROS_VFS_CONTROL_PREPARE_IMAGE
            || command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
            || command == MICROS_VFS_CONTROL_COMMIT_HIDDEN)
    ) {
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        return PM_COORDINATOR_REJECTED;
    }
    if (command == MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS
        && result == MICROS_VFS_CONTROL_RESULT_DESCRIPTOR) {
        spawn->rejection = MICROS_PM_RESULT_DESCRIPTOR;
        return PM_COORDINATOR_REJECTED;
    }
    if (command != MICROS_VFS_CONTROL_PREPARE_IMAGE) {
        return PM_COORDINATOR_CORRUPT;
    }
    switch (result) {
    case MICROS_VFS_CONTROL_RESULT_NOT_FOUND:
        spawn->rejection = MICROS_PM_RESULT_NOT_FOUND;
        break;
    case MICROS_VFS_CONTROL_RESULT_ACCESS:
        spawn->rejection = MICROS_PM_RESULT_ACCESS;
        break;
    case MICROS_VFS_CONTROL_RESULT_TOO_BIG:
        spawn->rejection = MICROS_PM_RESULT_TOO_BIG;
        break;
    case MICROS_VFS_CONTROL_RESULT_IO:
        spawn->rejection = MICROS_PM_RESULT_IO;
        break;
    case MICROS_VFS_CONTROL_RESULT_EXECUTABLE:
        spawn->rejection = MICROS_PM_RESULT_EXECUTABLE;
        break;
    default:
        return PM_COORDINATOR_CORRUPT;
    }
    return PM_COORDINATOR_REJECTED;
}

enum pm_coordinator_step pm_coordinator_vfs_call(
    struct pm_coordinator_spawn *spawn, uint32_t command,
    const uint64_t arguments[5], struct micros_pm_coordinator_control_result *result
)
{
    struct micros_ipc_message message;

    if (
        micros_pm_coordinator_build_vfs_control(command, arguments, &message)
            != MICROS_PM_COORDINATOR_PROTOCOL_OK
        || spawn->backend->call(spawn->context, spawn->coordinator->bindings.vfs, &message)
            != MICROS_SYSCALL_ABI_OK
        || micros_pm_coordinator_decode_vfs_result(
            spawn->coordinator->bindings.vfs, command, &message, result
        ) != MICROS_PM_COORDINATOR_PROTOCOL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    return pm_coordinator_classify_vfs_result(spawn, command, result->result);
}

enum pm_coordinator_step pm_coordinator_vm_call(
    struct pm_coordinator_spawn *spawn, uint32_t command,
    const uint64_t arguments[5], struct micros_pm_coordinator_control_result *result
)
{
    struct micros_ipc_message message;

    if (
        micros_pm_coordinator_build_vm_control(command, arguments, &message)
            != MICROS_PM_COORDINATOR_PROTOCOL_OK
        || spawn->backend->call(spawn->context, spawn->coordinator->bindings.vm, &message)
            != MICROS_SYSCALL_ABI_OK
        || micros_pm_coordinator_decode_vm_result(
            spawn->coordinator->bindings.vm, command, &message, result
        ) != MICROS_PM_COORDINATOR_PROTOCOL_OK
    ) {
        return PM_COORDINATOR_CORRUPT;
    }
    if (result->result == MICROS_VM_CONTROL_RESULT_OK) {
        return PM_COORDINATOR_ADVANCED;
    }
    if (command != MICROS_VM_CONTROL_LOAD) {
        return PM_COORDINATOR_CORRUPT;
    }
    switch (result->result) {
    case MICROS_VM_CONTROL_RESULT_NO_SPACE:
        spawn->rejection = MICROS_PM_RESULT_NO_SPACE;
        break;
    case MICROS_VM_CONTROL_RESULT_EXECUTABLE:
        spawn->rejection = MICROS_PM_RESULT_EXECUTABLE;
        break;
    case MICROS_VM_CONTROL_RESULT_TOO_BIG:
        spawn->rejection = MICROS_PM_RESULT_TOO_BIG;
        break;
    default:
        return PM_COORDINATOR_CORRUPT;
    }
    return PM_COORDINATOR_REJECTED;
}
