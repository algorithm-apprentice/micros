#include "kernel/tests/endpoint/endpoint_test_internal.h"

#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/kernel_object_runtime.h"

static const struct micros_privilege_profile profiles[2] = {
    {
        .id = 1,
        .name = "CLIENT",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .call_targets = UINT32_C(1) << 2,
        .notify_targets = UINT32_C(1) << 1,
    },
    {
        .id = 2,
        .name = "SERVER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .send_targets = UINT32_C(1) << 1,
        .notify_targets = UINT32_C(1) << 2,
    },
};
static const struct micros_privilege_profile invalid_profile = {
    .id = 0,
    .name = "INVALID",
};

bool endpoint_qemu_capture_baseline(struct endpoint_qemu_work *work)
{
    work->objects = micros_kernel_object_runtime_test_registry();
    if (work->objects == NULL) {
        return false;
    }
    work->baseline_processes = work->objects->live_process_count;
    work->baseline_threads = work->objects->live_thread_count;
    work->baseline_harts = work->objects->registered_hart_count;
    return true;
}

bool endpoint_qemu_initialize_runtimes(struct endpoint_qemu_work *work)
{
    if (
        work->baseline_processes != 0
        || work->baseline_threads != 0
        || work->baseline_harts != 1
        || micros_ipc_runtime_registry() != NULL
        || micros_ipc_runtime_authoritative_registry() != NULL
        || micros_grant_runtime_registry() != NULL
        || micros_grant_runtime_authoritative_registry() != NULL
        || micros_grant_runtime_validate()
            != MICROS_GRANT_ERROR_NOT_INITIALIZED
        || micros_grant_runtime_initialize()
            != MICROS_GRANT_ERROR_NOT_INITIALIZED
        || micros_grant_runtime_registry() != NULL
        || micros_grant_runtime_authoritative_registry() != NULL
        || micros_ipc_runtime_validate()
            != MICROS_ENDPOINT_ERROR_NOT_INITIALIZED
        || micros_ipc_runtime_initialize(&invalid_profile, 1)
            != MICROS_ENDPOINT_ERROR_PROFILE
        || micros_ipc_runtime_registry() != NULL
        || micros_ipc_runtime_authoritative_registry() != NULL
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || micros_grant_runtime_initialize()
            != MICROS_GRANT_ERROR_ALREADY_INITIALIZED
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED
        || (
            work->registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            work->grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
    ) {
        return false;
    }
    return true;
}

bool endpoint_qemu_reserve_clients(struct endpoint_qemu_work *work)
{
    if (
        micros_process_create(work->objects, &work->processes[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_create(
            work->objects,
            work->processes[0],
            &work->threads[0]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(work->objects, &work->processes[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_create(
            work->objects,
            work->processes[1],
            &work->threads[1]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_reserve(
            work->registry,
            work->objects,
            work->processes[0],
            &work->endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_reserve(
            work->registry,
            work->objects,
            work->processes[1],
            &work->endpoints[1]
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return true;
}
