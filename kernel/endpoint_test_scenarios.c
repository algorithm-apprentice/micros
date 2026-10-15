#include "endpoint_test_internal.h"

#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/user_address_space.h"

bool endpoint_qemu_require_held_owners(struct endpoint_qemu_work *work)
{
    if (
        micros_thread_release(work->objects, work->threads[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(work->objects, work->processes[0])
            != MICROS_KERNEL_OBJECT_ERROR_STATE
        || micros_thread_create(
            work->objects,
            work->processes[0],
            &work->threads[0]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

bool endpoint_qemu_publish_and_check_authority(struct endpoint_qemu_work *work)
{
    work->record = (const struct micros_endpoint_record *)(uintptr_t)1;
    if (
        micros_endpoint_resolve_active(
            work->registry,
            work->objects,
            work->endpoints[0],
            &work->record
        ) != MICROS_ENDPOINT_ERROR_STATE
        || work->record
            != (const struct micros_endpoint_record *)(uintptr_t)1
        || micros_endpoint_install_profile(
            work->registry,
            work->objects,
            work->processes[0],
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            work->registry,
            work->objects,
            work->processes[1],
            2
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            work->registry,
            work->objects,
            work->endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            work->registry,
            work->objects,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_CALL,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_SEND,
            work->endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_CALL,
            work->endpoints[0]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[0]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            work->registry,
            work->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_process_release(work->objects, work->processes[0])
            != MICROS_KERNEL_OBJECT_ERROR_STATE
    ) {
        return false;
    }
    return true;
}

bool endpoint_qemu_prepare_grant_cancellation(struct endpoint_qemu_work *work)
{
    if (
        micros_grant_create(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[0],
            work->endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            128,
            MICROS_GRANT_PERMISSION_READ,
            &work->read_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[1],
            work->endpoints[0],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            128,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->write_grant
        ) != MICROS_GRANT_OK
        || micros_grant_create(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[0],
            work->endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x3000),
            64,
            MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE,
            &work->revoked_grant
        ) != MICROS_GRANT_OK
        || micros_grant_inspect(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[0],
            work->revoked_grant,
            &work->grant_record
        ) != MICROS_GRANT_OK
        || work->grant_record.permissions
            != (
                MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE
            )
        || micros_grant_inspect(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[1],
            work->revoked_grant,
            &work->grant_record
        ) != MICROS_GRANT_ERROR_UNAUTHORIZED
        || micros_grant_revoke(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[0],
            work->revoked_grant
        ) != MICROS_GRANT_OK
        || micros_grant_inspect(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[0],
            work->revoked_grant,
            &work->grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_grant_prepare_endpoint_cancel(
            work->grant_registry,
            work->registry,
            work->objects,
            work->endpoints[0],
            &work->cancel_plan
        ) != MICROS_GRANT_OK
    ) {
        return false;
    }
    return true;
}

bool endpoint_qemu_close_commit_and_reuse(struct endpoint_qemu_work *work)
{
    if (
        micros_ipc_endpoint_close(
            work->registry,
            work->objects,
            work->endpoints[0]
        ) != MICROS_IPC_OK
        || micros_grant_commit_endpoint_cancel(
            work->grant_registry,
            &work->cancel_plan
        ) != MICROS_GRANT_OK
        || work->grant_registry->active_count != 0
        || micros_grant_inspect(
            work->grant_registry,
            work->registry,
            work->objects,
            work->processes[1],
            work->write_grant,
            &work->grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_thread_release(work->objects, work->threads[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(work->objects, work->processes[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(work->objects, &work->replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || work->replacement_process.slot != work->processes[0].slot
        || work->replacement_process.generation
            != work->processes[0].generation + 1
        || micros_thread_create(
            work->objects,
            work->replacement_process,
            &work->replacement_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_reserve(
            work->registry,
            work->objects,
            work->replacement_process,
            &work->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            work->registry,
            work->objects,
            work->replacement_process,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            work->registry,
            work->objects,
            work->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return true;
}

bool endpoint_qemu_reject_stale_and_cleanup(struct endpoint_qemu_work *work)
{
    work->record = (const struct micros_endpoint_record *)(uintptr_t)1;
    if (
        work->replacement_endpoint == work->endpoints[0]
        || micros_endpoint_resolve_internal(
            work->registry,
            work->objects,
            work->endpoints[0],
            &work->record
        ) != MICROS_ENDPOINT_ERROR_STALE
        || work->record
            != (const struct micros_endpoint_record *)(uintptr_t)1
        || micros_grant_inspect(
            work->grant_registry,
            work->registry,
            work->objects,
            work->replacement_process,
            work->read_grant,
            &work->grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || micros_endpoint_close(
            work->registry,
            work->objects,
            work->replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_close(
            work->registry,
            work->objects,
            work->endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_thread_release(work->objects, work->replacement_thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(work->objects, work->replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_release(work->objects, work->threads[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(work->objects, work->processes[1])
            != MICROS_KERNEL_OBJECT_OK
        || work->objects->live_process_count != work->baseline_processes
        || work->objects->live_thread_count != work->baseline_threads
        || work->objects->registered_hart_count != work->baseline_harts
        || micros_endpoint_registry_validate_objects(
            work->registry,
            work->objects
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
    ) {
        return false;
    }
    return true;
}
