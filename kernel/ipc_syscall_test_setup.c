#include "ipc_syscall_test_internal.h"

bool ipc_syscall_test_prepare_process(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    const unsigned char *payload,
    size_t payload_size
)
{
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    struct micros_user_context context;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;

    if (
        micros_user_address_space_create(fixture->processes[index])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &fixture->code_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->data_physical[index][0]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_DATA_SECOND_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->data_physical[index][1]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_STACK_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->stack_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            fixture->processes[index],
            &fixture->threads[index]
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            fixture->threads[index],
            &kernel_stack_bottom,
            &kernel_stack_top
        )
    ) {
        return false;
    }
    (void)kernel_stack_bottom;
    (void)kernel_stack_top;
    ipc_syscall_test_copy_bytes(
        (void *)(uintptr_t)fixture->code_physical[index],
        payload,
        payload_size
    );
    ipc_syscall_test_clear_bytes(
        (void *)(uintptr_t)fixture->data_physical[index][0],
        MICROS_SV39_PAGE_SIZE
    );
    ipc_syscall_test_clear_bytes(
        (void *)(uintptr_t)fixture->data_physical[index][1],
        MICROS_SV39_PAGE_SIZE
    );
    ipc_syscall_test_fill_context_pattern(
        &context,
        UINT64_C(0x1000) + index * UINT64_C(0x1000)
    );
    context.sepc =
        ipc_syscall_test_user_address_of(micros_ipc_syscall_test_payload_ecall);
    context.sp =
        IPC_SYSCALL_TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.a4 = UINT64_C(0x4400) + index;
    context.a5 = UINT64_C(0x5500) + index;
    context.a6 = UINT64_C(0x6600) + index;
    context.sstatus = 0;
    if (index == IPC_SYSCALL_CLIENT) {
        ipc_syscall_test_arm_send(
            fixture,
            index,
            &context,
            IPC_COMMAND_CLIENT_SEND_ONE,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1001),
            UINT8_C(0x10)
        );
    } else if (index == IPC_SYSCALL_SERVER) {
        ipc_syscall_test_arm_receive(
            fixture,
            index,
            &context,
            IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
            MICROS_ENDPOINT_ANY
        );
    } else {
        ipc_syscall_test_arm_send(
            fixture,
            index,
            &context,
            IPC_COMMAND_PEER_SEND,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1501),
            UINT8_C(0x60)
        );
    }
    return micros_user_execution_prepare(
        fixture->threads[index],
        &context
    ) == MICROS_USER_EXECUTION_OK;
}

bool ipc_syscall_test_cleanup_process(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index
)
{
    uint64_t released;

    return (
        micros_thread_scheduler_remove(
            objects,
            fixture->threads[index]
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_execution_detach(fixture->threads[index])
            == MICROS_USER_EXECUTION_OK
        && micros_thread_release(objects, fixture->threads[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_release_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == fixture->code_physical[index]
        && micros_user_address_space_release_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == fixture->data_physical[index][0]
        && micros_user_address_space_release_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_DATA_SECOND_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == fixture->data_physical[index][1]
        && micros_user_address_space_release_page(
            fixture->processes[index],
            IPC_SYSCALL_TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == fixture->stack_physical[index]
        && micros_user_address_space_destroy(fixture->processes[index])
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_runtime_release_process(
            objects,
            fixture->processes[index]
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool initialize_syscall_fixture_runtime(
    struct ipc_syscall_test_context *fixture,
    struct ipc_syscall_setup_state *state
)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "SYSCALL_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY,
            .call_targets = UINT32_C(1) << 2,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "SYSCALL_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 3,
        },
        {
            .id = 3,
            .name = "SYSCALL_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << 2,
            .notify_targets = UINT32_C(1) << 2,
        },
    };
    state->ledger = micros_frame_ownership_runtime_ledger();
    state->objects = micros_kernel_object_runtime_test_registry();
    if (state->ledger == NULL || state->objects == NULL) {
        return false;
    }
    fixture->baseline_owned = state->ledger->owned_frame_count;
    fixture->baseline_free = state->ledger->allocator->free_frame_count;
    fixture->baseline_processes = state->objects->live_process_count;
    fixture->baseline_threads = state->objects->live_thread_count;
    fixture->baseline_harts = state->objects->registered_hart_count;
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    fixture->registry = micros_ipc_runtime_authoritative_registry();
    state->payload_size =
        (uintptr_t)micros_ipc_syscall_test_payload_end
        - (uintptr_t)micros_ipc_syscall_test_payload_start;
    if (
        fixture->registry == NULL
        || state->payload_size == 0
        || state->payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        return false;
    }
    return true;
}

static bool prepare_syscall_fixture_endpoints(
    struct ipc_syscall_test_context *fixture,
    struct ipc_syscall_setup_state *state
)
{
    for (state->index = 0; state->index < IPC_SYSCALL_PROCESS_COUNT; ++state->index) {
        if (
            micros_process_create(state->objects, &fixture->processes[state->index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                fixture->registry,
                state->objects,
                fixture->processes[state->index],
                &fixture->endpoints[state->index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                fixture->registry,
                state->objects,
                fixture->processes[state->index],
                (uint8_t)(state->index + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                fixture->registry,
                state->objects,
                fixture->endpoints[state->index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_syscall_fixture_execution(
    struct ipc_syscall_test_context *fixture,
    struct ipc_syscall_setup_state *state
)
{
    for (state->index = 0; state->index < IPC_SYSCALL_PROCESS_COUNT; ++state->index) {
        if (
            !ipc_syscall_test_prepare_process(
                fixture,
                state->objects,
                state->index,
                micros_ipc_syscall_test_payload_start,
                state->payload_size
            )
        ) {
            return false;
        }
    }
    return true;
}

bool ipc_syscall_test_prepare_environment(
    struct ipc_syscall_test_context *fixture,
    struct ipc_syscall_setup_state *state
)
{
    return (
        initialize_syscall_fixture_runtime(fixture, state)
        && prepare_syscall_fixture_endpoints(fixture, state)
        && prepare_syscall_fixture_execution(fixture, state)
    );
}
