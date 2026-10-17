#include "kernel/tests/grant/grant_syscall_test_internal.h"

bool grant_syscall_test_release_address_space(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    size_t page_index;

    if (!fixture->address_space_live[actor]) {
        return true;
    }
    for (
        page_index = 0;
        page_index < GRANT_SYSCALL_PAGE_COUNT;
        ++page_index
    ) {
        uint64_t released = 0;

        if (
            micros_user_address_space_release_page(
                fixture->processes[actor],
                TEST_CODE_ADDRESS
                    + page_index * MICROS_SV39_PAGE_SIZE,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released != fixture->user_physical[actor][page_index]
        ) {
            return false;
        }
        fixture->user_physical[actor][page_index] = 0;
    }
    if (
        micros_user_address_space_destroy(fixture->processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    fixture->address_space_live[actor] = false;
    (void)objects;
    return true;
}

bool grant_syscall_test_release_thread(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    if (!fixture->thread_live[actor]) {
        return true;
    }
    if (
        micros_thread_scheduler_remove(objects, fixture->threads[actor])
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(fixture->threads[actor])
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, fixture->threads[actor])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    fixture->thread_live[actor] = false;
    return true;
}

bool grant_syscall_test_close_endpoint(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_grant_cancel_plan plan;

    return (
        micros_grant_prepare_endpoint_cancel(
            fixture->grant_registry,
            fixture->endpoint_registry,
            objects,
            fixture->endpoints[actor],
            &plan
        ) == MICROS_GRANT_OK
        && micros_ipc_endpoint_close(
            fixture->endpoint_registry,
            objects,
            fixture->endpoints[actor]
        ) == MICROS_IPC_OK
        && micros_grant_commit_endpoint_cancel(
            fixture->grant_registry,
            &plan
        ) == MICROS_GRANT_OK
    );
}

bool grant_syscall_test_reuse_grantor(
    struct grant_syscall_test_fixture *fixture
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_process_handle replacement;
    micros_endpoint_t replacement_endpoint;
    uint32_t old_generation =
        fixture->processes[GRANT_SYSCALL_GRANTOR].generation;
    uint16_t old_slot = fixture->processes[GRANT_SYSCALL_GRANTOR].slot;

    fixture->old_grantor_endpoint = fixture->endpoints[GRANT_SYSCALL_GRANTOR];
    if (
        objects == NULL
        || !grant_syscall_test_close_endpoint(fixture, objects, GRANT_SYSCALL_GRANTOR)
        || !grant_syscall_test_release_thread(fixture, objects, GRANT_SYSCALL_GRANTOR)
        || !grant_syscall_test_release_address_space(
            fixture,
            objects,
            GRANT_SYSCALL_GRANTOR
        )
        || micros_frame_ownership_runtime_release_process(
            objects,
            fixture->processes[GRANT_SYSCALL_GRANTOR]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement)
            != MICROS_KERNEL_OBJECT_OK
        || replacement.slot != old_slot
        || replacement.generation != old_generation + 1
        || micros_endpoint_reserve(
            fixture->endpoint_registry,
            objects,
            replacement,
            &replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            fixture->endpoint_registry,
            objects,
            replacement,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            fixture->endpoint_registry,
            objects,
            replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || replacement_endpoint == fixture->old_grantor_endpoint
    ) {
        return false;
    }
    fixture->processes[GRANT_SYSCALL_GRANTOR] = replacement;
    fixture->endpoints[GRANT_SYSCALL_GRANTOR] = replacement_endpoint;
    return (
        fixture->grant_registry->active_count == 0
        && micros_grant_runtime_validate() == MICROS_GRANT_OK
        && micros_ipc_runtime_validate() == MICROS_ENDPOINT_OK
        && micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_frame_ownership_runtime_validate(objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

bool grant_syscall_test_prepare_process(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor,
    const unsigned char *payload,
    size_t payload_size
)
{
    static const uint32_t page_permissions[
        GRANT_SYSCALL_PAGE_COUNT
    ] = {
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_READ,
    };
    struct micros_user_context context;
    size_t page_index;

    if (
        micros_user_address_space_create(fixture->processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    fixture->address_space_live[actor] = true;
    for (
        page_index = 0;
        page_index < GRANT_SYSCALL_PAGE_COUNT;
        ++page_index
    ) {
        if (
            micros_user_address_space_allocate_page(
                fixture->processes[actor],
                TEST_CODE_ADDRESS
                    + page_index * MICROS_SV39_PAGE_SIZE,
                page_permissions[page_index],
                &fixture->user_physical[actor][page_index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            return false;
        }
        grant_syscall_test_fill_bytes(
            (void *)(uintptr_t)
                fixture->user_physical[actor][page_index],
            (unsigned char)(UINT8_C(0xc0) + actor),
            MICROS_SV39_PAGE_SIZE
        );
    }
    if (
        micros_thread_create(
            objects,
            fixture->processes[actor],
            &fixture->threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    fixture->thread_live[actor] = true;
    grant_syscall_test_copy_bytes(
        (void *)(uintptr_t)fixture->user_physical[actor][0],
        payload,
        payload_size
    );
    grant_syscall_test_fill_context_pattern(
        &context,
        UINT64_C(0x1000) + actor * UINT64_C(0x1000)
    );
    context.sepc =
        grant_syscall_test_user_address_of(micros_grant_syscall_payload_spin);
    context.sp = TEST_STACK_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.sstatus = 0;
    return micros_user_execution_prepare(fixture->threads[actor], &context)
        == MICROS_USER_EXECUTION_OK;
}

bool grant_syscall_test_endpoint_registry_is_empty(
    struct grant_syscall_test_fixture *fixture
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            fixture->endpoint_registry->endpoints[index].state
                != MICROS_ENDPOINT_STATE_FREE
        ) {
            return false;
        }
    }
    return true;
}

bool grant_syscall_test_scheduler_is_at_idle_baseline(
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
