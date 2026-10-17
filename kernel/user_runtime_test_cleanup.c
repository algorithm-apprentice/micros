#include "kernel/user_runtime_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static bool user_runtime_test_configurations_complete(
    const struct user_runtime_test_context *runtime
)
{
    static const uint64_t role_status[USER_RUNTIME_PROCESS_COUNT] = {
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_REGISTERS
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
    };
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        struct micros_user_runtime_test_config config;

        if (
            !user_runtime_test_read_configuration(runtime, actor, &config)
            || config.version != MICROS_USER_RUNTIME_TEST_VERSION
            || config.role != actor + 1
            || config.client_endpoint != runtime->endpoints[USER_RUNTIME_CLIENT]
            || config.server_endpoint != runtime->endpoints[USER_RUNTIME_SERVER]
            || config.peer_endpoint != runtime->endpoints[USER_RUNTIME_PEER]
            || config.status != role_status[actor]
            || config.failure != 0
        ) {
            return false;
        }
    }
    return true;
}

static bool user_runtime_test_close_endpoint(
    const struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_grant_cancel_plan plan;
    enum micros_grant_error grant_error;
    enum micros_ipc_error ipc_error;

    grant_error = micros_grant_prepare_endpoint_cancel(
        runtime->grant_registry,
        runtime->endpoint_registry,
        objects,
        runtime->endpoints[actor],
        &plan
    );
    if (grant_error != MICROS_GRANT_OK) {
        uart_write("MICROS_USER_RUNTIME_CLOSE grant-prepare=");
        uart_write_hex64(grant_error);
        uart_write("\n");
        return false;
    }
    ipc_error = micros_ipc_endpoint_close(
        runtime->endpoint_registry,
        objects,
        runtime->endpoints[actor]
    );
    if (ipc_error != MICROS_IPC_OK) {
        const struct micros_thread *thread =
            &objects->threads[runtime->threads[actor].slot];

        uart_write("MICROS_USER_RUNTIME_CLOSE ipc=");
        uart_write_hex64(ipc_error);
        uart_write(" actor=");
        uart_write_hex64(actor);
        uart_write(" flags=");
        uart_write_hex64(thread->runtime_flags);
        uart_write(" queue=");
        uart_write_hex64(thread->ipc_queue_kind);
        uart_write(" pending=");
        uart_write_hex64(thread->ipc_delivery_pending);
        uart_write(" result=");
        uart_write_hex64(thread->ipc_staged_result);
        uart_write("\n");
        return false;
    }
    grant_error = micros_grant_commit_endpoint_cancel(
        runtime->grant_registry,
        &plan
    );
    if (grant_error != MICROS_GRANT_OK) {
        uart_write("MICROS_USER_RUNTIME_CLOSE grant-commit=");
        uart_write_hex64(grant_error);
        uart_write("\n");
        return false;
    }
    return true;
}

static bool user_runtime_test_release_process(
    const struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    uint64_t released;
    size_t page_index;

    if (
        micros_thread_scheduler_remove(
            objects,
            runtime->threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(runtime->threads[actor])
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, runtime->threads[actor])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (
        page_index = 0;
        page_index < runtime->images[actor].page_count;
        ++page_index
    ) {
        if (
            micros_user_address_space_release_page(
                runtime->processes[actor],
                runtime->images[actor].pages[page_index].virtual_address,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released
                != runtime->images[actor].pages[page_index].physical_address
        ) {
            return false;
        }
    }
    if (
        micros_user_address_space_release_page(
            runtime->processes[actor],
            MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != runtime->images[actor].stack_physical
        || micros_user_address_space_destroy(runtime->processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            runtime->processes[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

static bool user_runtime_test_endpoint_registry_is_empty(
    const struct user_runtime_test_context *runtime
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            runtime->endpoint_registry->endpoints[index].state
                != MICROS_ENDPOINT_STATE_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool user_runtime_test_scheduler_is_idle(
    const struct micros_kernel_objects *objects
)
{
    const struct micros_hart *hart =
        &objects->harts[
            micros_kernel_object_runtime_boot_hart_handle().slot
        ];
    size_t priority;

    if (
        hart->current_thread.slot != 0
        || hart->current_thread.generation != 0
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
    ) {
        return false;
    }
    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (
            hart->ready_head[priority].slot != 0
            || hart->ready_head[priority].generation != 0
            || hart->ready_tail[priority].slot != 0
            || hart->ready_tail[priority].generation != 0
        ) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_user_runtime_test_finish(void)
{
    struct user_runtime_test_context *runtime = user_runtime_test_state();
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *kernel_space =
        micros_kernel_address_space_report();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || kernel_space == NULL
        || objects == NULL
        || runtime->endpoint_registry == NULL
        || runtime->grant_registry == NULL
        || !runtime->loader_validated
        || !runtime->rodata_fault_seen
        || runtime->returned_actor != USER_RUNTIME_SERVER
        || !user_runtime_test_configurations_complete(runtime)
    ) {
        goto failure;
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!user_runtime_test_close_endpoint(runtime, objects, actor)) {
            failure_stage = UINT64_C(0x10) + actor;
            goto failure;
        }
    }
    failure_stage = 2;
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!user_runtime_test_release_process(runtime, objects, actor)) {
            failure_stage = UINT64_C(0x20) + actor;
            goto failure;
        }
    }
    failure_stage = 3;
    if (
        runtime->grant_registry->active_count != 0
        || !user_runtime_test_endpoint_registry_is_empty(runtime)
        || objects->live_process_count != runtime->baseline_processes
        || objects->live_thread_count != runtime->baseline_threads
        || objects->registered_hart_count != runtime->baseline_harts
        || ledger->owned_frame_count != runtime->baseline_owned
        || ledger->allocator->free_frame_count != runtime->baseline_free
        || !user_runtime_test_scheduler_is_idle(objects)
        || user_runtime_test_read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (kernel_space->root_physical_address >> 12)
            )
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    user_runtime_test_report_success();

failure:
    user_runtime_test_report_failure(runtime, failure_stage);
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        user_runtime_test_report_actor(runtime, actor);
    }
    user_runtime_test_report_failure_shutdown();
}
