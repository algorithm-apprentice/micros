#include "ipc_test_internal.h"

static bool initialize_trusted_ipc_authority_and_runtime(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = IPC_TEST_CLIENT_PROFILE,
            .name = "IPC_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .call_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .send_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
        },
        {
            .id = IPC_TEST_SERVER_PROFILE,
            .name = "IPC_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
        },
        {
            .id = IPC_TEST_PEER_PROFILE,
            .name = "IPC_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .call_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .send_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
        },
    };
    state->objects = micros_kernel_object_runtime_test_registry();
    state->ledger = micros_frame_ownership_runtime_ledger();
    state->boot_hart = micros_kernel_object_runtime_boot_hart();
    state->hart = micros_kernel_object_runtime_boot_hart_handle();
    if (state->objects == NULL || state->boot_hart == NULL || state->ledger == NULL) {
        return false;
    }
    state->baseline_processes = state->objects->live_process_count;
    state->baseline_threads = state->objects->live_thread_count;
    state->baseline_harts = state->objects->registered_hart_count;
    state->baseline_owned = state->ledger->owned_frame_count;
    state->baseline_free = state->ledger->allocator->free_frame_count;
    ipc_test_copy_bytes(&fixture->hart_snapshot, state->boot_hart, sizeof(fixture->hart_snapshot));
    if (
        state->baseline_processes != 0
        || state->baseline_threads != 0
        || state->baseline_harts != 1
        || !ipc_test_thread_handle_is_zero(state->boot_hart->current_thread)
        || micros_ipc_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || (
            fixture->registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return true;
}

static bool prepare_trusted_ipc_processes(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    static const uint8_t profile_ids[IPC_TEST_PROCESS_COUNT] = {
        IPC_TEST_CLIENT_PROFILE,
        IPC_TEST_SERVER_PROFILE,
        IPC_TEST_PEER_PROFILE,
    };
    for (state->index = 0; state->index < IPC_TEST_PROCESS_COUNT; ++state->index) {
        if (
            micros_process_create(state->objects, &state->processes[state->index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(state->processes[state->index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_endpoint_reserve(
                fixture->registry,
                state->objects,
                state->processes[state->index],
                &state->endpoints[state->index]
            ) != MICROS_ENDPOINT_OK
            || micros_thread_create(
                state->objects,
                state->processes[state->index],
                &state->threads[state->index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_install_profile(
                fixture->registry,
                state->objects,
                state->processes[state->index],
                profile_ids[state->index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                fixture->registry,
                state->objects,
                state->endpoints[state->index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_trusted_ipc_execution(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    for (state->index = 0; state->index < IPC_TEST_PROCESS_COUNT; ++state->index) {
        struct micros_user_context context =
            ipc_test_context_pattern(UINT64_C(0x2000) + state->index * UINT64_C(0x100));
        uintptr_t stack_bottom =
            (uintptr_t)&fixture->ipc_test_stacks[state->index][0];
        uintptr_t stack_top =
            (uintptr_t)&fixture->ipc_test_stacks[state->index][
                MICROS_THREAD_KERNEL_STACK_SIZE
            ];

        if (
            micros_thread_attach_execution_context(
                state->objects,
                state->threads[state->index],
                stack_bottom,
                stack_top,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                state->objects,
                state->hart,
                state->threads[state->index],
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                state->objects,
                state->threads[state->index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    if (
        micros_endpoint_registry_validate_objects(fixture->registry, state->objects)
            != MICROS_ENDPOINT_OK
        || !ipc_test_ready_queue_matches(state->objects, state->hart, NULL, 0)
        || !ipc_test_snapshot_execution_state(fixture, state->objects, state->processes, state->threads)
    ) {
        return false;
    }
    state->failure_stage = 2;

    return true;
}

bool ipc_test_prepare_environment(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    return (
        initialize_trusted_ipc_authority_and_runtime(fixture, state)
        && prepare_trusted_ipc_processes(fixture, state)
        && prepare_trusted_ipc_execution(fixture, state)
    );
}
