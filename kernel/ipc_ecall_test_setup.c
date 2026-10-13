#include "ipc_ecall_test_internal.h"

static bool prepare_ecall_runtime_and_endpoints(
    struct ipc_ecall_test_context *fixture,
    struct ipc_ecall_setup_state *state
)
{
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
            ipc_ecall_test_profiles,
            sizeof(ipc_ecall_test_profiles) / sizeof(ipc_ecall_test_profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    fixture->registry = micros_ipc_runtime_authoritative_registry();
    if (fixture->registry == NULL) {
        return false;
    }
    for (state->index = 0; state->index < IPC_ECALL_PROCESS_COUNT; ++state->index) {
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
                ipc_ecall_test_profile_ids[state->index]
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

static bool prepare_ecall_client_image_and_thread(
    struct ipc_ecall_test_context *fixture,
    struct ipc_ecall_setup_state *state
)
{
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    state->payload_size =
        (uintptr_t)micros_ipc_ecall_test_payload_end
        - (uintptr_t)micros_ipc_ecall_test_payload_start;
    if (
        state->payload_size == 0
        || state->payload_size > MICROS_SV39_PAGE_SIZE
        || micros_user_address_space_create(
            fixture->processes[IPC_ECALL_CLIENT]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &fixture->code_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_DATA_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->data_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[IPC_ECALL_CLIENT],
            IPC_ECALL_TEST_STACK_VIRTUAL_ADDRESS,
            data_permissions,
            &fixture->stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            state->objects,
            fixture->processes[IPC_ECALL_CLIENT],
            &fixture->client_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            fixture->client_thread,
            &state->kernel_stack_bottom,
            &state->kernel_stack_top
        )
    ) {
        return false;
    }
    return true;
}

static bool prepare_ecall_payload_and_context(
    struct ipc_ecall_test_context *fixture,
    struct ipc_ecall_setup_state *state
)
{
    (void)state->kernel_stack_bottom;
    (void)state->kernel_stack_top;
    ipc_ecall_test_copy_bytes(
        (void *)(uintptr_t)fixture->code_physical,
        micros_ipc_ecall_test_payload_start,
        state->payload_size
    );
    ipc_ecall_test_clear_bytes((void *)(uintptr_t)fixture->data_physical, MICROS_SV39_PAGE_SIZE);
    state->message = (struct micros_ipc_message *)(uintptr_t)fixture->data_physical;
    state->message->type = UINT32_C(0x7101);
    for (state->index = 0; state->index < sizeof(state->message->payload); ++state->index) {
        state->message->payload[state->index] = (uint8_t)(UINT8_C(0x40) + state->index);
    }
    ipc_ecall_test_clear_bytes(&state->context, sizeof(state->context));
    state->context.sepc = IPC_ECALL_TEST_CODE_VIRTUAL_ADDRESS;
    state->context.sp =
        IPC_ECALL_TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    state->context.s0 =
        IPC_ECALL_TEST_DATA_VIRTUAL_ADDRESS + IPC_ECALL_TEST_CONTROL_OFFSET;
    state->context.s1 = fixture->endpoints[IPC_ECALL_SERVER];
    state->context.s2 = IPC_ECALL_TEST_DATA_VIRTUAL_ADDRESS;
    state->context.a0 = fixture->endpoints[IPC_ECALL_SERVER];
    state->context.a1 = UINT64_C(5);
    state->context.a4 = IPC_ECALL_TEST_A4;
    state->context.a5 = IPC_ECALL_TEST_A5;
    state->context.a6 = IPC_ECALL_TEST_A6;
    state->context.a7 = MICROS_IPC_ABI_NOTIFY;
    if (
        micros_user_execution_prepare(fixture->client_thread, &state->context)
            != MICROS_USER_EXECUTION_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    return true;
}

bool ipc_ecall_test_prepare_environment(
    struct ipc_ecall_test_context *fixture,
    struct ipc_ecall_setup_state *state
)
{
    return (
        prepare_ecall_runtime_and_endpoints(fixture, state)
        && prepare_ecall_client_image_and_thread(fixture, state)
        && prepare_ecall_payload_and_context(fixture, state)
    );
}
