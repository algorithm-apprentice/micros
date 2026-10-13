#include "ipc_test_internal.h"

bool ipc_test_cleanup_resources(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    if (
        !ipc_test_execution_state_matches(fixture, state->objects, state->processes, state->threads)
        || micros_ipc_endpoint_close(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_PEER]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            fixture->registry,
            state->objects,
            state->endpoints[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    for (state->index = 0; state->index < IPC_TEST_PROCESS_COUNT; ++state->index) {
        if (
            !ipc_test_cleanup_thread(state->objects, state->threads[state->index])
            || micros_user_address_space_destroy(state->processes[state->index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_process_release(state->objects, state->processes[state->index])
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    state->failure_stage = 11;
    return true;
}

bool ipc_test_validate_final_baseline(
    struct ipc_test_context *fixture,
    struct ipc_test_run_state *state
)
{
    if (
        state->objects->live_process_count != state->baseline_processes
        || state->objects->live_thread_count != state->baseline_threads
        || state->objects->registered_hart_count != state->baseline_harts
        || state->ledger->owned_frame_count != state->baseline_owned
        || state->ledger->allocator->free_frame_count != state->baseline_free
    ) {
        state->failure_stage = UINT64_C(0x1101);
        return false;
    }
    if (fixture->registry->last_reply_token < state->close_token) {
        state->failure_stage = UINT64_C(0x1102);
        return false;
    }
    if (!ipc_test_endpoint_records_are_clear(fixture)) {
        state->failure_stage = UINT64_C(0x1103);
        return false;
    }
    if (!ipc_test_bytes_equal(state->boot_hart, &fixture->hart_snapshot, sizeof(*state->boot_hart))) {
        state->failure_stage = UINT64_C(0x1104);
        return false;
    }
    if (
        micros_frame_ownership_runtime_validate(state->objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        state->failure_stage = UINT64_C(0x1108);
        return false;
    }
    if (
        micros_kernel_objects_validate(state->objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        state->failure_stage = UINT64_C(0x1105);
        return false;
    }
    if (
        micros_scheduler_core_validate(state->objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        state->failure_stage = UINT64_C(0x1106);
        return false;
    }
    if (
        micros_endpoint_registry_validate_objects(fixture->registry, state->objects)
            != MICROS_ENDPOINT_OK
    ) {
        state->failure_stage = UINT64_C(0x1107);
        return false;
    }
    return true;
}

bool ipc_test_report_result(const struct ipc_test_run_state *state)
{
    if (!state->passed) {
        uart_write("MICROS_TEST_FAILURE ipc-stage=");
        uart_write_hex64(state->failure_stage);
        uart_write("\n");
        uart_flush();
    }
    riscv_irq_restore(state->saved_status);
    return state->passed;
}
