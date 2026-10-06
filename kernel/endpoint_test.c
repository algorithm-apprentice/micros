#include "micros/endpoint.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "micros/kernel_object_runtime.h"

static struct micros_endpoint_registry registry;

bool micros_endpoint_runtime_run_self_test(void);

bool micros_endpoint_runtime_run_self_test(void)
{
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
    struct micros_kernel_objects *objects;
    struct micros_process_handle processes[2];
    struct micros_process_handle replacement_process;
    struct micros_thread_handle threads[2];
    struct micros_thread_handle replacement_thread;
    micros_endpoint_t endpoints[2];
    micros_endpoint_t replacement_endpoint;
    const struct micros_endpoint_record *record;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    uintptr_t saved_status;
    bool passed = false;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    if (objects == NULL) {
        goto done;
    }
    baseline_processes = objects->live_process_count;
    baseline_threads = objects->live_thread_count;
    baseline_harts = objects->registered_hart_count;
    if (
        baseline_processes != 0
        || baseline_threads != 0
        || baseline_harts != 1
        || micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_process_create(objects, &processes[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_create(
            objects,
            processes[0],
            &threads[0]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &processes[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_create(
            objects,
            processes[1],
            &threads[1]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_reserve(
            &registry,
            objects,
            processes[0],
            &endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_reserve(
            &registry,
            objects,
            processes[1],
            &endpoints[1]
        ) != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }
    if (
        micros_thread_release(objects, threads[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(objects, processes[0])
            != MICROS_KERNEL_OBJECT_ERROR_STATE
        || micros_thread_create(
            objects,
            processes[0],
            &threads[0]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }

    record = (const struct micros_endpoint_record *)(uintptr_t)1;
    if (
        micros_endpoint_resolve_active(
            &registry,
            objects,
            endpoints[0],
            &record
        ) != MICROS_ENDPOINT_ERROR_STATE
        || record
            != (const struct micros_endpoint_record *)(uintptr_t)1
        || micros_endpoint_install_profile(
            &registry,
            objects,
            processes[0],
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            &registry,
            objects,
            processes[1],
            2
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            &registry,
            objects,
            endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            &registry,
            objects,
            endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_CALL,
            endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            endpoints[1]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[1]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_SEND,
            endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_CALL,
            endpoints[0]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[0]
        ) != MICROS_ENDPOINT_ERROR_UNAUTHORIZED
        || micros_endpoint_authorize_target(
            &registry,
            objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_process_release(objects, processes[0])
            != MICROS_KERNEL_OBJECT_ERROR_STATE
    ) {
        goto done;
    }

    if (
        micros_endpoint_close(
            &registry,
            objects,
            endpoints[0]
        ) != MICROS_ENDPOINT_OK
        || micros_thread_release(objects, threads[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(objects, processes[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || replacement_process.slot != processes[0].slot
        || replacement_process.generation
            != processes[0].generation + 1
        || micros_thread_create(
            objects,
            replacement_process,
            &replacement_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_reserve(
            &registry,
            objects,
            replacement_process,
            &replacement_endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }

    record = (const struct micros_endpoint_record *)(uintptr_t)1;
    if (
        replacement_endpoint == endpoints[0]
        || micros_endpoint_resolve_internal(
            &registry,
            objects,
            endpoints[0],
            &record
        ) != MICROS_ENDPOINT_ERROR_STALE
        || record
            != (const struct micros_endpoint_record *)(uintptr_t)1
        || micros_endpoint_close(
            &registry,
            objects,
            replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_close(
            &registry,
            objects,
            endpoints[1]
        ) != MICROS_ENDPOINT_OK
        || micros_thread_release(objects, replacement_thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(objects, replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_release(objects, threads[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_release(objects, processes[1])
            != MICROS_KERNEL_OBJECT_OK
        || objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || micros_endpoint_registry_validate_objects(
            &registry,
            objects
        ) != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }
    passed = true;

done:
    riscv_irq_restore(saved_status);
    return passed;
}
