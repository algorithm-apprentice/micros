#include "kernel/scheduler_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/timer.h"
#include "micros/user_execution.h"

static bool completion_state_is_clear(
    const struct micros_thread *thread
)
{
    static const struct micros_ipc_message zero_message;

    return (
        thread != NULL
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && scheduler_test_bytes_equal(
            &thread->ipc_inbound_message,
            &zero_message,
            sizeof(zero_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
    );
}

bool scheduler_test_observe_completion(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    enum test_completion_kind kind;
    const struct micros_thread *thread;

    if (objects == NULL || thread_index >= 2) {
        return false;
    }
    kind = fixture->expected_completion[thread_index];
    if (kind == TEST_COMPLETION_NONE) {
        return true;
    }
    thread = &objects->threads[fixture->threads[thread_index].slot];
    if (
        !completion_state_is_clear(thread)
        || thread->user_context.a0
            != fixture->expected_completion_result[thread_index]
    ) {
        return false;
    }
    if (
        kind == TEST_COMPLETION_MESSAGE
        && !scheduler_test_bytes_equal(
            (const void *)(uintptr_t)(
                fixture->counter_physical[thread_index] + UINT64_C(0x100)
            ),
            &fixture->expected_message,
            sizeof(fixture->expected_message)
        )
    ) {
        return false;
    }
    switch (kind) {
    case TEST_COMPLETION_START:
        fixture->start_completion_observed = true;
        break;
    case TEST_COMPLETION_MESSAGE:
        fixture->message_completion_observed = true;
        break;
    case TEST_COMPLETION_CAPTURED:
        fixture->captured_completion_observed = true;
        break;
    case TEST_COMPLETION_ERROR:
        fixture->error_completion_observed = true;
        break;
    case TEST_COMPLETION_IDLE:
        fixture->idle_completion_observed = true;
        break;
    case TEST_COMPLETION_EXTERNAL_IDLE:
        fixture->idle_external_completion_observed = true;
        break;
    case TEST_COMPLETION_NONE:
        return false;
    }
    fixture->expected_completion[thread_index] = TEST_COMPLETION_NONE;
    return true;
}

bool scheduler_test_expect_no_message_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index,
    enum micros_ipc_error result,
    enum test_completion_kind kind
)
{
    int64_t abi_result;

    if (objects == NULL || thread_index >= 2) {
        return false;
    }
    switch (result) {
    case MICROS_IPC_OK:
        abi_result = MICROS_IPC_ABI_OK;
        break;
    case MICROS_IPC_ERROR_DEAD_ENDPOINT:
        abi_result = MICROS_IPC_ABI_DEAD_ENDPOINT;
        break;
    default:
        return false;
    }
    if (
        micros_ipc_stage_no_message_completion(
            fixture->registry,
            objects,
            fixture->threads[thread_index],
            result
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    fixture->expected_completion[thread_index] = kind;
    fixture->expected_completion_result[thread_index] =
        (uint64_t)abi_result;
    fixture->prepared_contexts[thread_index].a0 =
        (uint64_t)abi_result;
    return true;
}

bool scheduler_test_expect_message_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    struct micros_thread *thread;

    if (
        objects == NULL
        || thread_index >= 2
    ) {
        return false;
    }
    thread = &objects->threads[fixture->threads[thread_index].slot];
    scheduler_test_clear_bytes(&fixture->expected_message, sizeof(fixture->expected_message));
    fixture->expected_message.source = MICROS_ENDPOINT_NONE;
    fixture->expected_message.type =
        MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    fixture->expected_message.payload[0] = UINT8_C(0x05);
    if (
        micros_thread_scheduler_hold(
            objects,
            fixture->threads[thread_index]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_receive(
            fixture->registry,
            objects,
            fixture->threads[thread_index],
            MICROS_ENDPOINT_ANY,
            TEST_IPC_BUFFER_VIRTUAL_ADDRESS
        ) != MICROS_IPC_OK
        || micros_ipc_inject_kernel_notification(
            fixture->registry,
            objects,
            fixture->endpoints[thread_index],
            UINT64_C(0x05)
        ) != MICROS_IPC_OK
        || !thread->ipc_delivery_pending
        || !scheduler_test_bytes_equal(
            &thread->ipc_inbound_message,
            &fixture->expected_message,
            sizeof(fixture->expected_message)
        )
    ) {
        return false;
    }
    fixture->expected_completion[thread_index] =
        TEST_COMPLETION_MESSAGE;
    fixture->expected_completion_result[thread_index] =
        MICROS_IPC_ABI_OK;
    fixture->prepared_contexts[thread_index].a0 =
        MICROS_IPC_ABI_OK;
    return micros_ipc_runtime_validate() == MICROS_ENDPOINT_OK;
}

enum micros_scheduler_test_trap_action
scheduler_test_completion_test_mismatch(uint64_t stage)
{
    uart_write("MICROS_TEST_FAILURE scheduler-completion-stage=");
    uart_write_hex64(stage);
    uart_write("\n");
    uart_flush();
    return MICROS_SCHEDULER_TEST_MISMATCH;
}

bool scheduler_test_user_registers_match(
    struct scheduler_test_context *fixture,
    size_t thread_index,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_user_context observed;
    struct micros_user_context expected;
    const struct micros_process *process;
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        thread_index >= 2
        || hart == NULL
        || frame == NULL
        || objects == NULL
        || micros_user_execution_inspect(
            fixture->threads[thread_index],
            &observed
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return false;
    }
    scheduler_test_copy_bytes(
        &expected,
        &fixture->prepared_contexts[thread_index],
        sizeof(expected)
    );
    expected.sepc = frame->sepc;
    expected.s1 = (
        frame->sepc < scheduler_test_user_address_of(micros_scheduler_payload_loop)
    ) ? 0 : 1;
    if (
        !scheduler_test_bytes_equal(&observed, &expected, sizeof(expected))
        || !scheduler_test_bytes_equal(&observed, frame, sizeof(observed))
        || hart->trap.primary_stack_bottom
            != fixture->kernel_stack_bottom[thread_index]
        || hart->trap.primary_stack_top
            != fixture->kernel_stack_top[thread_index]
        || !scheduler_test_thread_handles_equal(
            hart->ready_head[MICROS_SCHEDULER_PRIORITY_DEFAULT_USER],
            fixture->threads[thread_index]
        )
        || micros_process_resolve(
            objects,
            fixture->processes[thread_index],
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || scheduler_test_read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    ) {
        return false;
    }
    return true;
}

bool micros_scheduler_test_after_start(
    const struct micros_hart *hart
)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || scheduler_test_current_thread_index(fixture, hart) != 0
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || hart->accounting_started_at
            < micros_timer_test_last_program_counter()
        || hart->kernel_counter_ticks
            < fixture->kernel_ticks_before_start + TEST_START_TIMER_DELAY
        || objects->threads[fixture->threads[0].slot]
            .remaining_counter_ticks != TEST_QUANTUM
        || !scheduler_test_observe_completion(fixture, objects, 0)
    ) {
        return false;
    }
    fixture->start_boundary_observed = true;
    return true;
}

bool micros_scheduler_test_after_user_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct scheduler_test_context *fixture =
        scheduler_test_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t thread_index = scheduler_test_current_thread_index(fixture, hart);
    size_t index;

    if (
        thread_index >= 2
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || !scheduler_test_user_registers_match(fixture, thread_index, hart, frame)
        || !scheduler_test_observe_completion(fixture, objects, thread_index)
    ) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (
            index != thread_index
            && fixture->expected_completion[index]
                == TEST_COMPLETION_MESSAGE
            && objects->threads[fixture->threads[index].slot]
                .ipc_delivery_pending
        ) {
            fixture->message_completion_deferred = true;
        }
    }
    if (fixture->last_running == 2) {
        fixture->last_running = thread_index;
    } else if (fixture->last_running != thread_index) {
        fixture->last_running = thread_index;
        ++fixture->switch_count;
    }
    if (
        fixture->user_external_observed
        && !fixture->user_external_return_observed
    ) {
        fixture->user_external_return_observed = true;
    }
    return true;
}
